// Feedback Delay Network reverb (Jot & Chaigne, "Digital delay networks for
// designing artificial reverberators", AES 1991; see also Valimaki et al.
// "Fifty Years of Artificial Reverberation", 2012).
//
//   x[n]  -> [8 delay lines d_i] -> [damping LPF] -> y_i
//   feedback vector  f = g * H * y      (H = normalised 8x8 Hadamard)
//   line input       = predelayed mono input + f_i
//   output L/R       = fixed +/- taps of the 8 lines, width-controlled
//
// H is orthogonal (H^T H = I) so the recursion is lossless at g = 1; the
// global gain g maps from the decay control, and per-line damping controls
// HF decay. Line lengths are mutually prime-ish and slowly modulated so the
// modal response never rings on a single pitch. Freeze sets g = 1 and mutes
// the input for an infinite sustained tail.

#include "ReverbModule.h"

#include <cmath>

namespace audiorack
{

namespace
{
    // Base line lengths in ms at size = 1.0 (spread, mutually non-commensurate).
    constexpr double kLineMs[ReverbModule::kLines] =
        { 23.1, 29.7, 37.3, 43.9, 51.7, 59.3, 67.1, 73.9 };

    constexpr double kMaxPredelayMs = 200.0;

    // Fast in-place 8-point Hadamard transform (unnormalised); caller scales
    // by 1/sqrt(8). Butterfly network, 24 add/sub instead of 64 MACs.
    inline void hadamard8 (float* v) noexcept
    {
        for (int step = 1; step < 8; step <<= 1)
            for (int i = 0; i < 8; i += step << 1)
                for (int j = i; j < i + step; ++j)
                {
                    const float a = v[j];
                    const float b = v[j + step];
                    v[j]        = a + b;
                    v[j + step] = a - b;
                }
    }
}

ModuleTypeInfo ReverbModule::typeInfo()
{
    return {
        kDescriptor,
        [] { return std::unique_ptr<AudioModule> (std::make_unique<ReverbModule>()); },
        &ReverbModule::declareParameters
    };
}

void ReverbModule::declareParameters (ParameterBuilder& b)
{
    juce::NormalisableRange<float> preRange (0.0f, static_cast<float> (kMaxPredelayMs), 0.1f);
    preRange.setSkewForCentre (30.0f);

    b.add ({ "size",     "Size",     { 0.0f, 1.0f, 0.001f }, 0.5f, "" });
    b.add ({ "decay",    "Decay",    { 0.0f, 1.0f, 0.001f }, 0.6f, "" });
    b.add ({ "damping",  "Damping",  { 0.0f, 1.0f, 0.001f }, 0.5f, "" });
    b.add ({ "predelay", "Predelay", preRange, 20.0f, "ms" });
    b.add ({ "mix",      "Mix",      { 0.0f, 1.0f, 0.001f }, 0.3f, "" });
    b.add ({ "width",    "Width",    { 0.0f, 1.0f, 0.001f }, 1.0f, "" });
    b.add ({ "freeze",   "Freeze",   { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Off", "Freeze" } });
}

void ReverbModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;

    for (int i = 0; i < kLines; ++i)
    {
        // Max length at size = 1 plus modulation headroom.
        lines[i].prepare (kLineMs[i] * 0.001 * fs * 2.0 + 64.0);
        baseDelay[i] = kLineMs[i] * 0.001 * fs;
        modPhase[i]  = static_cast<double> (i) * 0.7;
    }

    for (auto& pd : predelay)
        pd.prepare (kMaxPredelayMs * 0.001 * fs + 64.0);

    smoothedMix.reset (fs, 0.03);
    smoothedDecay.reset (fs, 0.05);
    smoothedSize.reset (fs, 0.08);
    cachedDamp = -1.0;
    reset();
}

void ReverbModule::reset()
{
    for (int i = 0; i < kLines; ++i)
    {
        lines[i].reset();
        damp[i].reset();
        lineState[i] = 0.0f;
    }
    for (auto& pd : predelay)
        pd.reset();

    smoothedMix.setCurrentAndTargetValue (mix.get());
    smoothedDecay.setCurrentAndTargetValue (decay.get());
    smoothedSize.setCurrentAndTargetValue (size.get());
}

void ReverbModule::bindParameter (const juce::String& id, std::atomic<float>* v)
{
    if (v == nullptr) return;

    if      (id == "size")     size.value = v;
    else if (id == "decay")    decay.value = v;
    else if (id == "damping")  damping.value = v;
    else if (id == "predelay") predelayMs.value = v;
    else if (id == "mix")      mix.value = v;
    else if (id == "width")    width.value = v;
    else if (id == "freeze")   freeze.value = v;
}

void ReverbModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept
{
    const auto numSamples  = static_cast<int> (block.getNumSamples());
    const auto numChannels = static_cast<int> (block.getNumChannels());

    const bool frozen = freeze.get() >= 0.5f;

    // Damping LPF cutoff: damping 0 -> ~18 kHz (bright), 1 -> ~1.5 kHz (dark).
    const double dampParam = static_cast<double> (damping.get());
    if (! juce::exactlyEqual (dampParam, cachedDamp))
    {
        const double cutoff = 1500.0 + (1.0 - dampParam) * 16500.0;
        for (auto& f : damp)
            f.setLowpass (cutoff, 0.5, fs);
        cachedDamp = dampParam;
    }

    smoothedMix.setTargetValue (mix.get());
    smoothedDecay.setTargetValue (frozen ? 1.0f : decay.get());
    smoothedSize.setTargetValue (size.get());

    const float widthAmt   = width.get();
    const double preSamples = juce::jlimit (0.0, kMaxPredelayMs, static_cast<double> (predelayMs.get()))
                              * 0.001 * fs;
    const double modInc     = 2.0 * juce::MathConstants<double>::pi * 0.5 / fs;   // 0.5 Hz line mod
    const double modDepth   = 0.0015 * fs;

    float peak[2] = { 0.0f, 0.0f };
    double sumSq[2] = { 0.0, 0.0 };

    for (int i = 0; i < numSamples; ++i)
    {
        const float wet = smoothedMix.getNextValue();
        const float dry = 1.0f - wet;

        // Global feedback gain from decay: 0 -> ~0.5 s, 1 -> ~12 s RT60-ish.
        const float decayNow = smoothedDecay.getNextValue();
        const float g = frozen ? 1.0f : (0.70f + 0.298f * decayNow);
        const float sizeNow = smoothedSize.getNextValue();

        const float inL = block.getChannelPointer (0)[i];
        const float inR = numChannels > 1 ? block.getChannelPointer (1)[i] : inL;
        // Input keeps flowing in freeze (layer/shimmer use); g = 1 sustains
        // what is already in the network while new signal can still be added.
        const float monoIn = 0.5f * (inL + inR);

        predelay[0].write (monoIn);
        const float injected = predelay[0].read (preSamples);

        // Read all lines (damped), form the feedback via Hadamard.
        float v[kLines];
        for (int k = 0; k < kLines; ++k)
        {
            modPhase[k] += modInc;
            const double dlen = baseDelay[k] * (0.5 + 0.5 * sizeNow)
                                + modDepth * std::sin (modPhase[k]);
            float s = lines[k].read (dlen);
            s = damp[k].process (s);
            lineState[k] = s;
            v[k] = s;
        }

        hadamard8 (v);
        const float norm = g * 0.35355339f;    // g / sqrt(8)

        for (int k = 0; k < kLines; ++k)
        {
            const float injectGain = (k < 4) ? 1.0f : -1.0f;   // spread input across lines
            lines[k].write (injected * injectGain * 0.5f + v[k] * norm);
        }

        // Output taps: even lines -> L, odd -> R for stereo decorrelation.
        float wetL = 0.0f, wetR = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            if ((k & 1) == 0) wetL += lineState[k];
            else              wetR += lineState[k];
        }
        wetL *= 0.5f;
        wetR *= 0.5f;

        // Width: blend towards mono at width 0.
        const float mid  = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * widthAmt;
        wetL = mid + side;
        wetR = mid - side;

        const float outL = dry * inL + wet * wetL;
        const float outR = dry * inR + wet * wetR;

        block.getChannelPointer (0)[i] = outL;
        if (numChannels > 1)
            block.getChannelPointer (1)[i] = outR;

        peak[0] = juce::jmax (peak[0], std::abs (outL));
        peak[1] = juce::jmax (peak[1], std::abs (outR));
        sumSq[0] += static_cast<double> (outL) * static_cast<double> (outL);
        sumSq[1] += static_cast<double> (outR) * static_cast<double> (outR);
    }

    meterPeakL.store (peak[0], std::memory_order_relaxed);
    meterPeakR.store (peak[1], std::memory_order_relaxed);
    const auto n = static_cast<double> (juce::jmax (1, numSamples));
    meterRmsL.store (static_cast<float> (std::sqrt (sumSq[0] / n)), std::memory_order_relaxed);
    meterRmsR.store (static_cast<float> (std::sqrt (sumSq[1] / n)), std::memory_order_relaxed);
}

void ReverbModule::getMeterFrame (MeterFrame& frame) const noexcept
{
    frame.peakL = meterPeakL.load (std::memory_order_relaxed);
    frame.peakR = meterPeakR.load (std::memory_order_relaxed);
    frame.rmsL  = meterRmsL.load (std::memory_order_relaxed);
    frame.rmsR  = meterRmsR.load (std::memory_order_relaxed);
    frame.gainReductionDb = 0.0f;
}

} // namespace audiorack
