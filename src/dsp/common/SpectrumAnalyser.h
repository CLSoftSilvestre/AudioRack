#pragma once

// Real-time spectrum analyser shared by every module that draws an RTA
// (currently EQ-6; the M8 Meter/Analyzer module reuses it unchanged).
//
// Split across two threads on purpose:
//
//   audio thread    push()   — mono-sums the block and copies it into a plain
//                              circular buffer, publishing the write position
//                              with a release store. No FFT, no allocation, no
//                              locks: a memcpy-shaped cost proportional to the
//                              block length.
//   message thread  render() — takes the newest kFftSize samples, windows them
//                              and runs one real FFT, then folds the linear
//                              bins into log-spaced bands.
//
// Single producer / single consumer. The ring is kRingFactor x the FFT window,
// so the writer must run ahead by more than (kRingFactor - 1) windows —
// ~256 ms at 48 kHz — before it can overwrite samples the reader is copying.
// A UI poll that late has already dropped many frames; the worst case is one
// torn display frame, never a crash or an audio-thread stall.
//
// Analysis follows the standard periodogram recipe (Harris, "On the use of
// windows for harmonic analysis with the discrete Fourier transform", 1978):
// Hann window, magnitude spectrum, amplitude normalised by the window's
// coherent gain so a full-scale sine reads 0 dBFS in its band.

#include "../../core/CoreTypes.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace audiorack::dsp
{

class SpectrumAnalyser
{
public:
    static constexpr int kFftOrder   = 12;             // 4096 points: 11.7 Hz bins at 48 kHz,
    static constexpr int kFftSize    = 1 << kFftOrder; // enough to separate the bottom octaves
    static constexpr int kRingFactor = 4;
    static constexpr int kRingSize   = kRingFactor * kFftSize;

    SpectrumAnalyser()
    {
        // Coherent gain: a sine of amplitude A windowed by w has a peak bin
        // magnitude of A * sum(w) / 2, so this is the divisor that maps a
        // full-scale sine back to 1.0. Measured off the real table rather than
        // assumed, so changing the window type stays correct.
        std::fill (scratch.begin(), scratch.begin() + kFftSize, 1.0f);
        window.multiplyWithWindowingTable (scratch.data(), static_cast<size_t> (kFftSize));

        double sum = 0.0;
        for (int i = 0; i < kFftSize; ++i)
            sum += static_cast<double> (scratch[static_cast<size_t> (i)]);
        windowGain = static_cast<float> (sum * 0.5);

        scratch.fill (0.0f);
        smoothed.fill (kSpectrumFloorDb);
    }

    // --- message thread ------------------------------------------------------

    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        buildBandEdges();
        reset();
    }

    void reset() noexcept
    {
        ring.fill (0.0f);
        writePos.store (0, std::memory_order_release);
        lastRenderPos = -1;
        smoothed.fill (kSpectrumFloorDb);
    }

    /** Runs the FFT and fills `out` with the current banded magnitude in dBFS.
        Call from the message thread at UI rate (~30 Hz). Returns false only
        before prepare(); when no new audio has arrived since the last call the
        display simply decays, so a stopped transport fades out instead of
        freezing.
    */
    bool render (SpectrumFrame& out) noexcept
    {
        if (fs <= 0.0)
            return false;

        const auto pos = writePos.load (std::memory_order_acquire);

        if (pos != lastRenderPos)
        {
            lastRenderPos = pos;

            // Copy the newest kFftSize samples, oldest first.
            const auto start = pos - kFftSize;
            for (int i = 0; i < kFftSize; ++i)
            {
                const auto index = (start + i) % kRingSize;
                scratch[static_cast<size_t> (i)] =
                    ring[static_cast<size_t> (index < 0 ? index + kRingSize : index)];
            }
            std::fill (scratch.begin() + kFftSize, scratch.end(), 0.0f);

            window.multiplyWithWindowingTable (scratch.data(), static_cast<size_t> (kFftSize));
            fft.performFrequencyOnlyForwardTransform (scratch.data(), true);

            foldIntoBands();
        }
        else
        {
            // No new audio: decay towards the floor at the release rate.
            for (auto& db : smoothed)
                db += (kSpectrumFloorDb - db) * kRelease;
        }

        out.db = smoothed;
        return true;
    }

    // --- audio thread --------------------------------------------------------

    /** Mono-sums `block` into the ring. Realtime-safe. */
    void push (const juce::dsp::AudioBlock<const float>& block) noexcept
    {
        const auto numSamples  = static_cast<int> (block.getNumSamples());
        const auto numChannels = static_cast<int> (block.getNumChannels());

        if (numSamples <= 0 || numChannels <= 0)
            return;

        const float scale = 1.0f / static_cast<float> (numChannels);
        auto pos = writePos.load (std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += block.getSample (ch, i);

            ring[static_cast<size_t> (pos % kRingSize)] = sum * scale;
            ++pos;
        }

        writePos.store (pos, std::memory_order_release);
    }

private:
    static constexpr float kRelease = 0.30f;  // per render call; ~4 calls (130 ms at 30 Hz)
                                              // to fall 90 % of the way to a lower level

    void buildBandEdges() noexcept
    {
        const auto binWidth = fs / static_cast<double> (kFftSize);
        const auto ratio    = static_cast<double> (kSpectrumMaxHz / kSpectrumMinHz);
        const auto nyquist  = static_cast<float> (kFftSize / 2);

        // Edges stay *fractional* bins. A log band set is much finer than the
        // linear bin grid at the bottom of the range — at 48 kHz the whole
        // span below ~450 Hz is narrower than one bin per band — so snapping
        // edges to integers and forcing them apart would march the low bands
        // steadily up in frequency. foldIntoBands() interpolates instead.
        for (int b = 0; b <= kSpectrumBands; ++b)
        {
            const auto hz = kSpectrumMinHz
                            * std::pow (ratio, static_cast<double> (b) / kSpectrumBands);
            edges[static_cast<size_t> (b)] =
                std::clamp (static_cast<float> (hz / binWidth), 1.0f, nyquist);
        }
    }

    /// Magnitude at a fractional bin, linearly interpolated. Recovers most of
    /// the Hann scalloping loss for tones that fall between bins.
    float magnitudeAt (float bin) const noexcept
    {
        const auto i = std::clamp (static_cast<int> (bin), 1, kFftSize / 2);
        const auto j = std::min (i + 1, kFftSize / 2);
        const auto f = bin - static_cast<float> (i);
        return scratch[static_cast<size_t> (i)] * (1.0f - f)
             + scratch[static_cast<size_t> (j)] * f;
    }

    void foldIntoBands() noexcept
    {
        for (int b = 0; b < kSpectrumBands; ++b)
        {
            const auto lo = edges[static_cast<size_t> (b)];
            const auto hi = edges[static_cast<size_t> (b + 1)];

            // Peak, not mean: a narrow tone must not be averaged away by the
            // wide bands at the top of the range. Both fractional edges are
            // included, so adjacent bands overlap slightly rather than leaving
            // a tone to fall down a gap between them.
            float mag = std::max (magnitudeAt (lo), magnitudeAt (hi));

            const auto first = static_cast<int> (std::ceil (lo));
            const auto last  = static_cast<int> (std::floor (hi));
            for (int bin = first; bin <= last; ++bin)
                mag = std::max (mag, scratch[static_cast<size_t> (bin)]);

            const auto db = std::max (kSpectrumFloorDb,
                                      juce::Decibels::gainToDecibels (mag / windowGain,
                                                                      kSpectrumFloorDb));

            auto& s = smoothed[static_cast<size_t> (b)];
            s = db > s ? db                      // instant rise: transients read true
                       : s + (db - s) * kRelease; // slow fall: the eye can follow it
        }
    }

    juce::dsp::FFT                     fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { kFftSize,
                                                 juce::dsp::WindowingFunction<float>::hann,
                                                 false };

    std::array<float, kRingSize>        ring {};
    std::atomic<long long>              writePos { 0 };   // total samples ever written

    std::array<float, 2 * kFftSize>     scratch {};       // FFT needs 2x for the complex output
    std::array<float, kSpectrumBands + 1> edges {};   // fractional FFT bins
    std::array<float, kSpectrumBands>   smoothed {};

    long long lastRenderPos = -1;
    double    fs            = 0.0;
    float     windowGain    = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyser)
};

} // namespace audiorack::dsp
