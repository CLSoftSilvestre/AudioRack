#pragma once

#include "../../core/ModuleRegistry.h"
#include "../common/Biquad.h"
#include "../common/FractionalDelay.h"

#include <atomic>

namespace audiorack
{

/** DL-2 — stereo delay with digital and tape modes, host-sync'd note
    divisions, ping-pong, feedback tone filtering and tape wow/flutter.
    Fractional delay via 4-point Lagrange so time changes don't zipper.
    See docs/modules/delay.md.
*/
class DelayModule final : public AudioModule
{
public:
    static constexpr ModuleDescriptor kDescriptor { "delay", "Delay", "Time", 2 };

    static ModuleTypeInfo typeInfo();
    static void declareParameters (ParameterBuilder&);

    ModuleDescriptor descriptor() const override { return kDescriptor; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void bindParameter (const juce::String& idSuffix, std::atomic<float>* value) override;

    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept override;
    void getMeterFrame (MeterFrame&) const noexcept override;

private:
    double targetDelaySamples (int channel, const ProcessContext&) const noexcept;

    struct Param
    {
        std::atomic<float>* value;
        explicit Param (float def) : value (&fallback), fallback (def) {}
        float get() const noexcept { return value->load (std::memory_order_relaxed); }
        std::atomic<float> fallback;
    };

    Param timeMs   { 350.0f }, sync { 0.0f }, division { 3.0f };
    Param feedback { 0.35f },  mix { 0.30f }, mode { 0.0f };
    Param pingpong { 0.0f },   toneHz { 6000.0f };
    Param flutter  { 0.20f },  stereoOffset { 0.0f };

    static constexpr int kChannels = 2;

    double fs = 44100.0;
    dsp::FractionalDelay lines[kChannels];
    dsp::Biquad          feedbackTone[kChannels];   // LPF in the feedback path (tape darkening)
    juce::SmoothedValue<float> smoothedDelay[kChannels];
    juce::SmoothedValue<float> smoothedFeedback, smoothedMix;

    double lfoPhase = 0.0;                            // wow/flutter LFO
    float  feedbackState[kChannels] = { 0.0f, 0.0f };
    double cachedTone = -1.0;

    std::atomic<float> meterPeakL { 0.0f }, meterPeakR { 0.0f };
    std::atomic<float> meterRmsL { 0.0f }, meterRmsR { 0.0f };
};

} // namespace audiorack
