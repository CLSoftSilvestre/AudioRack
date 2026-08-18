// Lookahead true-peak limiter.
//
// Gain chain (cf. G. Skiadopoulos / signalsmith-audio "Perfect limiter"
// construction; window/support relationship guarantees no overshoot):
//
//   d[n]   = max over channels and 4x-oversampled sub-samples of |x|
//            (4x polyphase FIR via juce::dsp::Oversampling, integer latency d)
//   t[n]   = min(1, c / d[n])                    c = ceiling (linear)
//   env[n] = t[n] if t[n] < env[n-1]             (fall: instant)
//            aR env[n-1] + (1-aR) t[n] otherwise (rise: release one-pole)
//   min:     sliding minimum over the last L+1 samples of env
//   smooth:  two cascaded boxcars of length floor(L/2)+1 each
//            (total support 2 floor(L/2)+1 <= L+1, so the smoothed gain never
//            exceeds any envelope value inside the min window -> the sample
//            being limited always receives a gain <= its own requirement)
//   out    = x delayed by (L + d) * gain, hard-clamped at c as a final guard
//
// Reported latency = L + d, recomputed when the lookahead parameter changes.

#include "LimiterModule.h"

#include <cmath>

namespace audiorack
{

namespace
{
    constexpr double kMaxLookaheadMs = 10.0;

    float fromDb (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
}

ModuleTypeInfo LimiterModule::typeInfo()
{
    return {
        kDescriptor,
        [] { return std::unique_ptr<AudioModule> (std::make_unique<LimiterModule>()); },
        &LimiterModule::declareParameters
    };
}

void LimiterModule::declareParameters (ParameterBuilder& b)
{
    auto skewed = [] (float min, float max, float centre)
    {
        juce::NormalisableRange<float> r (min, max, 0.01f);
        r.setSkewForCentre (centre);
        return r;
    };

    b.add ({ "ceiling",   "Ceiling",   { -20.0f, 0.0f, 0.1f }, -0.3f, "dBTP" });
    b.add ({ "release",   "Release",   skewed (1.0f, 1000.0f, 100.0f), 100.0f, "ms" });
    b.add ({ "lookahead", "Lookahead", skewed (0.5f, 10.0f, 2.0f), 2.0f, "ms" });
}

void LimiterModule::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs       = sampleRate;
    channels = numChannels;

    maxLookaheadSamples = static_cast<int> (std::ceil (kMaxLookaheadMs * 0.001 * fs)) + 1;

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        1, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true /*maxQuality*/, true /*integerLatency*/);
    oversampler->initProcessing (static_cast<size_t> (maxBlockSize));
    oversamplerLatency = static_cast<int> (std::ceil (oversampler->getLatencyInSamples()));

    detectorBuffer.setSize (1, maxBlockSize, false, false, true);

    mainDelay.prepare (maxLookaheadSamples + oversamplerLatency, channels);
    runningMin.prepare (maxLookaheadSamples + 1);
    smootherA.prepare (maxLookaheadSamples / 2 + 1);
    smootherB.prepare (maxLookaheadSamples / 2 + 1);

    cachedReleaseMs   = -1.0;
    cachedLookaheadMs = -1.0f;
    applyLookahead();
    reset();
}

void LimiterModule::reset()
{
    if (oversampler != nullptr)
        oversampler->reset();

    mainDelay.reset();
    runningMin.reset();
    smootherA.reset();
    smootherB.reset();
    releaseEnv = 1.0;
    meterGrDb.store (0.0f, std::memory_order_relaxed);
}

void LimiterModule::bindParameter (const juce::String& id, std::atomic<float>* v)
{
    if (v == nullptr) return;

    if      (id == "ceiling")   ceiling.value = v;
    else if (id == "release")   releaseMs.value = v;
    else if (id == "lookahead") lookahead.value = v;
}

void LimiterModule::applyLookahead() noexcept
{
    const float lookaheadParam = lookahead.get();

    if (juce::exactlyEqual (lookaheadParam, cachedLookaheadMs))
        return;

    cachedLookaheadMs = lookaheadParam;

    const int L = juce::jlimit (1, maxLookaheadSamples,
                                static_cast<int> (std::round (static_cast<double> (lookaheadParam) * 0.001 * fs)));

    mainDelay.setDelay (L + oversamplerLatency);
    runningMin.setWindow (L + 1);
    smootherA.setLength (L / 2 + 1);
    smootherB.setLength (L / 2 + 1);

    totalLatency.store (L + oversamplerLatency, std::memory_order_relaxed);
}

void LimiterModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept
{
    applyLookahead();

    const double rel = static_cast<double> (releaseMs.get());
    if (! juce::exactlyEqual (rel, cachedReleaseMs))
    {
        releaseCoef = std::exp (-1.0 / (rel * 0.001 * fs));
        cachedReleaseMs = rel;
    }

    const auto numSamples  = static_cast<int> (block.getNumSamples());
    const auto numChannels = static_cast<int> (block.getNumChannels());
    const float ceilLin    = fromDb (ceiling.get());

    // --- true-peak detector: mono max, 4x oversampled ------------------------
    auto* det = detectorBuffer.getWritePointer (0);

    for (int i = 0; i < numSamples; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            m = juce::jmax (m, std::abs (block.getChannelPointer (static_cast<size_t> (ch))[i]));
        det[i] = m;
    }

    juce::dsp::AudioBlock<float> monoBlock (detectorBuffer.getArrayOfWritePointers(), 1,
                                            static_cast<size_t> (numSamples));
    auto upBlock = oversampler->processSamplesUp (monoBlock);
    const auto* up = upBlock.getChannelPointer (0);

    float blockMinGain = 1.0f;
    float peak[2] = { 0.0f, 0.0f };
    double sumSq[2] = { 0.0, 0.0 };

    for (int i = 0; i < numSamples; ++i)
    {
        float d = det[i];
        for (int k = 0; k < 4; ++k)
            d = juce::jmax (d, std::abs (up[static_cast<size_t> (i) * 4 + static_cast<size_t> (k)]));

        const float target = d <= ceilLin ? 1.0f : ceilLin / d;

        // release: fall instantly, rise at the release rate
        if (static_cast<double> (target) < releaseEnv)
            releaseEnv = static_cast<double> (target);
        else
            releaseEnv = releaseCoef * releaseEnv + (1.0 - releaseCoef) * static_cast<double> (target);

        const float g = smootherB.process (
                            smootherA.process (
                                runningMin.process (static_cast<float> (releaseEnv))));

        blockMinGain = juce::jmin (blockMinGain, g);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (static_cast<size_t> (ch));
            const float delayed = mainDelay.process (ch, data[i]);
            const float out     = juce::jlimit (-ceilLin, ceilLin, delayed * g);
            data[i] = out;

            if (ch < 2)
            {
                peak[static_cast<size_t> (ch)] = juce::jmax (peak[static_cast<size_t> (ch)], std::abs (out));
                sumSq[static_cast<size_t> (ch)] += static_cast<double> (out) * static_cast<double> (out);
            }
        }

        mainDelay.advance();
    }

    meterGrDb.store (-20.0f * std::log10 (juce::jmax (1.0e-3f, blockMinGain)),
                     std::memory_order_relaxed);
    meterPeakL.store (peak[0], std::memory_order_relaxed);
    meterPeakR.store (peak[1], std::memory_order_relaxed);

    const auto n = static_cast<double> (juce::jmax (1, numSamples));
    meterRmsL.store (static_cast<float> (std::sqrt (sumSq[0] / n)), std::memory_order_relaxed);
    meterRmsR.store (static_cast<float> (std::sqrt (sumSq[1] / n)), std::memory_order_relaxed);
}

void LimiterModule::getMeterFrame (MeterFrame& frame) const noexcept
{
    frame.peakL = meterPeakL.load (std::memory_order_relaxed);
    frame.peakR = meterPeakR.load (std::memory_order_relaxed);
    frame.rmsL  = meterRmsL.load (std::memory_order_relaxed);
    frame.rmsR  = meterRmsR.load (std::memory_order_relaxed);
    frame.gainReductionDb = meterGrDb.load (std::memory_order_relaxed);
}

} // namespace audiorack
