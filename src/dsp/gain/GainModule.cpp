// Gain: y[n] = g[n] * x[n] with g ramped linearly over 10 ms towards
// 10^(dB/20) (juce::Decibels), which keeps step changes free of zipper noise.
// -60 dB is treated as -inf (true mute). Metering is block peak and block RMS
// measured post-gain; ballistics are applied UI-side.

#include "GainModule.h"

#include <cmath>

namespace audiorack
{

namespace
{
    constexpr float kMinusInfDb = -60.0f;
    constexpr double kRampSeconds = 0.01;
}

ModuleTypeInfo GainModule::typeInfo()
{
    return {
        kDescriptor,
        [] { return std::unique_ptr<AudioModule> (std::make_unique<GainModule>()); },
        &GainModule::declareParameters
    };
}

void GainModule::declareParameters (ParameterBuilder& builder)
{
    juce::NormalisableRange<float> range (kMinusInfDb, 12.0f, 0.01f);
    range.setSkewForCentre (0.0f);

    builder.add ({ "gaindb", "Gain", range, 0.0f, "dB" });
    builder.add ({ "chmode", "Channels", { 0.0f, 3.0f, 1.0f }, 0.0f, "",
                   { "Stereo", "Mono", "Left", "Right" } });
}

void GainModule::prepare (double sampleRate, int, int)
{
    gain.reset (sampleRate, kRampSeconds);
    reset();
}

void GainModule::reset()
{
    gain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (gainDb->load (std::memory_order_relaxed), kMinusInfDb));

    meterPeakL.store (0.0f, std::memory_order_relaxed);
    meterPeakR.store (0.0f, std::memory_order_relaxed);
    meterRmsL.store (0.0f, std::memory_order_relaxed);
    meterRmsR.store (0.0f, std::memory_order_relaxed);
}

void GainModule::bindParameter (const juce::String& idSuffix, std::atomic<float>* value)
{
    if (idSuffix == "gaindb" && value != nullptr)
        gainDb = value;
    else if (idSuffix == "chmode" && value != nullptr)
        chMode = value;
}

void GainModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept
{
    gain.setTargetValue (
        juce::Decibels::decibelsToGain (gainDb->load (std::memory_order_relaxed), kMinusInfDb));

    const auto numSamples  = block.getNumSamples();
    const auto numChannels = block.getNumChannels();

    // Channel conditioning (before the gain). Stereo (0) passes through; the
    // others collapse to a mono signal duplicated across L and R.
    const int mode = static_cast<int> (std::lround (chMode->load (std::memory_order_relaxed)));

    if (mode != 0 && numChannels >= 2)
    {
        float* left  = block.getChannelPointer (0);
        float* right = block.getChannelPointer (1);

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float m = mode == 1 ? 0.5f * (left[i] + right[i])   // Mono  (−6 dB sum)
                          : mode == 2 ? left[i]                       // Left  → both
                                      : right[i];                     // Right → both
            left[i]  = m;
            right[i] = m;
        }
    }

    for (size_t i = 0; i < numSamples; ++i)
    {
        const float g = gain.getNextValue();

        for (size_t ch = 0; ch < numChannels; ++ch)
            block.getChannelPointer (ch)[i] *= g;
    }

    // Post-gain block metering (peak + RMS per channel).
    const size_t meterChannels = numChannels < 2 ? numChannels : 2;

    for (size_t ch = 0; ch < meterChannels; ++ch)
    {
        const float* data = block.getChannelPointer (ch);

        float peak = 0.0f, sumSquares = 0.0f;

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float v = data[i];
            peak = juce::jmax (peak, std::abs (v));
            sumSquares += v * v;
        }

        const float rms = numSamples > 0
                            ? std::sqrt (sumSquares / static_cast<float> (numSamples))
                            : 0.0f;

        (ch == 0 ? meterPeakL : meterPeakR).store (peak, std::memory_order_relaxed);
        (ch == 0 ? meterRmsL  : meterRmsR ).store (rms,  std::memory_order_relaxed);
    }
}

void GainModule::getMeterFrame (MeterFrame& frame) const noexcept
{
    frame.peakL = meterPeakL.load (std::memory_order_relaxed);
    frame.peakR = meterPeakR.load (std::memory_order_relaxed);
    frame.rmsL  = meterRmsL.load (std::memory_order_relaxed);
    frame.rmsR  = meterRmsR.load (std::memory_order_relaxed);
    frame.gainReductionDb = 0.0f;
}

} // namespace audiorack
