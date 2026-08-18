// Six-band parametric EQ. All coefficient sets are RBJ Audio-EQ-Cookbook
// biquads (see Biquad.h); coefficients recompute only when a band parameter
// actually changes, so a static EQ costs six biquads per channel and nothing
// else. Solo semantics: when any band is soloed, only soloed (and enabled)
// bands are in the chain — standard "listen to what this band does" audition.

#include "EqModule.h"

#include <cmath>

namespace audiorack
{

ModuleTypeInfo EqModule::typeInfo()
{
    return {
        kDescriptor,
        [] { return std::unique_ptr<AudioModule> (std::make_unique<EqModule>()); },
        &EqModule::declareParameters
    };
}

void EqModule::declareParameters (ParameterBuilder& b)
{
    const float defaults[kBands] = { 60.0f, 150.0f, 400.0f, 1000.0f, 3500.0f, 10000.0f };

    juce::NormalisableRange<float> freqRange (20.0f, 20000.0f, 0.1f);
    freqRange.setSkewForCentre (632.0f);            // log-ish: 632 = sqrt(20*20000)

    juce::NormalisableRange<float> qRange (0.1f, 10.0f, 0.01f);
    qRange.setSkewForCentre (0.71f);

    for (int i = 0; i < kBands; ++i)
    {
        const auto n = juce::String (i);
        const auto bandName = "Band " + juce::String (i + 1) + " ";

        b.add ({ "b" + n + "type", bandName + "Type", { 0.0f, 4.0f, 1.0f }, 0.0f, "",
                 { "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass" } });
        b.add ({ "b" + n + "freq", bandName + "Freq", freqRange, defaults[i], "Hz" });
        b.add ({ "b" + n + "gain", bandName + "Gain", { -18.0f, 18.0f, 0.1f }, 0.0f, "dB" });
        b.add ({ "b" + n + "q",    bandName + "Q",    qRange, 0.71f, "" });
        b.add ({ "b" + n + "on",   bandName + "On",   { 0.0f, 1.0f, 1.0f }, 1.0f, "", { "Off", "On" } });
        b.add ({ "b" + n + "solo", bandName + "Solo", { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Off", "Solo" } });
    }

    b.add ({ "trim", "Output Trim", { -12.0f, 12.0f, 0.1f }, 0.0f, "dB" });
}

void EqModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    trimGain.reset (fs, 0.02);

    for (auto& band : bands)
        band.cachedType = -1.0f;    // force refresh

    reset();
}

void EqModule::reset()
{
    for (auto& band : bands)
        for (auto& f : band.filter)
            f.reset();

    trimGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (trim.get()));
}

void EqModule::bindParameter (const juce::String& id, std::atomic<float>* v)
{
    if (v == nullptr) return;

    if (id == "trim") { trim.value = v; return; }

    for (int i = 0; i < kBands; ++i)
    {
        const auto prefix = "b" + juce::String (i);
        if (! id.startsWith (prefix)) continue;

        auto& band = bands[static_cast<size_t> (i)];
        const auto field = id.fromFirstOccurrenceOf (prefix, false, false);

        if      (field == "type") band.type.value = v;
        else if (field == "freq") band.freq.value = v;
        else if (field == "gain") band.gain.value = v;
        else if (field == "q")    band.q.value = v;
        else if (field == "on")   band.on.value = v;
        else if (field == "solo") band.solo.value = v;
        return;
    }
}

void EqModule::refreshBand (int bandIndex) noexcept
{
    auto& band = bands[static_cast<size_t> (bandIndex)];

    const float type = band.type.get();
    const float freq = band.freq.get();
    const float gain = band.gain.get();
    const float q    = band.q.get();

    if (juce::exactlyEqual (type, band.cachedType) && juce::exactlyEqual (freq, band.cachedFreq)
        && juce::exactlyEqual (gain, band.cachedGain) && juce::exactlyEqual (q, band.cachedQ))
        return;

    band.cachedType = type;
    band.cachedFreq = freq;
    band.cachedGain = gain;
    band.cachedQ    = q;

    for (auto& f : band.filter)
    {
        const auto fd = static_cast<double> (freq);
        const auto qd = static_cast<double> (q);
        const auto gd = static_cast<double> (gain);

        switch (static_cast<int> (type))
        {
            case 0:  f.setPeak (fd, qd, gd, fs); break;
            case 1:  f.setLowShelf (fd, qd, gd, fs); break;
            case 2:  f.setHighShelf (fd, qd, gd, fs); break;
            case 3:  f.setHighpass (fd, qd, fs); break;
            case 4:  f.setLowpass (fd, qd, fs); break;
            default: f.setIdentity(); break;
        }
    }
}

void EqModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept
{
    const auto numSamples  = static_cast<int> (block.getNumSamples());
    const auto numChannels = static_cast<int> (block.getNumChannels());

    bool anySolo = false;
    for (auto& band : bands)
        anySolo = anySolo || band.solo.get() >= 0.5f;

    bool active[kBands];
    for (int i = 0; i < kBands; ++i)
    {
        auto& band = bands[static_cast<size_t> (i)];
        active[i] = band.on.get() >= 0.5f
                    && (! anySolo || band.solo.get() >= 0.5f);
        if (active[i])
            refreshBand (i);
    }

    trimGain.setTargetValue (juce::Decibels::decibelsToGain (trim.get()));

    float peak[2] = { 0.0f, 0.0f };
    double sumSq[2] = { 0.0, 0.0 };

    for (int i = 0; i < numSamples; ++i)
    {
        const float trimNow = trimGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (static_cast<size_t> (ch));
            float x = data[i];

            for (int bIdx = 0; bIdx < kBands; ++bIdx)
                if (active[bIdx])
                    x = bands[static_cast<size_t> (bIdx)].filter[ch < 2 ? ch : 0].process (x);

            x *= trimNow;
            data[i] = x;

            if (ch < 2)
            {
                peak[static_cast<size_t> (ch)] = juce::jmax (peak[static_cast<size_t> (ch)], std::abs (x));
                sumSq[static_cast<size_t> (ch)] += static_cast<double> (x) * static_cast<double> (x);
            }
        }
    }

    meterPeakL.store (peak[0], std::memory_order_relaxed);
    meterPeakR.store (peak[1], std::memory_order_relaxed);

    const auto n = static_cast<double> (juce::jmax (1, numSamples));
    meterRmsL.store (static_cast<float> (std::sqrt (sumSq[0] / n)), std::memory_order_relaxed);
    meterRmsR.store (static_cast<float> (std::sqrt (sumSq[1] / n)), std::memory_order_relaxed);
}

void EqModule::getMeterFrame (MeterFrame& frame) const noexcept
{
    frame.peakL = meterPeakL.load (std::memory_order_relaxed);
    frame.peakR = meterPeakR.load (std::memory_order_relaxed);
    frame.rmsL  = meterRmsL.load (std::memory_order_relaxed);
    frame.rmsR  = meterRmsR.load (std::memory_order_relaxed);
    frame.gainReductionDb = 0.0f;
}

} // namespace audiorack
