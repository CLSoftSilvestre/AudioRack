#pragma once

#include "../../core/ModuleRegistry.h"
#include "../common/Biquad.h"

#include <atomic>
#include <memory>

namespace audiorack
{

/** SAT-1 — waveshaping saturator with tube, tape and transistor curves,
    4x oversampling with anti-imaging filtering (JUCE Oversampling), drive
    compensation and a bias control. See docs/modules/saturator.md.
*/
class SaturatorModule final : public AudioModule
{
public:
    static constexpr ModuleDescriptor kDescriptor { "sat", "Saturator", "Tone", 1 };

    static ModuleTypeInfo typeInfo();
    static void declareParameters (ParameterBuilder&);

    ModuleDescriptor descriptor() const override { return kDescriptor; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void bindParameter (const juce::String& idSuffix, std::atomic<float>* value) override;

    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept override;

    int  latencySamples() const noexcept override { return latency.load (std::memory_order_relaxed); }
    void getMeterFrame (MeterFrame&) const noexcept override;

private:
    struct Param
    {
        std::atomic<float>* value;
        explicit Param (float def) : value (&fallback), fallback (def) {}
        float get() const noexcept { return value->load (std::memory_order_relaxed); }
        std::atomic<float> fallback;
    };

    Param driveDb { 6.0f }, type { 0.0f }, bias { 0.0f };
    Param outDb { 0.0f }, mix { 1.0f }, autoGain { 1.0f };

    double fs = 44100.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    dsp::Biquad dcBlocker[2];
    juce::AudioBuffer<float> dryBuffer;              // pre-shaping copy for wet/dry

    juce::SmoothedValue<float> smoothedDrive, smoothedOut, smoothedMix;

    std::atomic<int>   latency { 0 };
    std::atomic<float> meterPeakL { 0.0f }, meterPeakR { 0.0f };
    std::atomic<float> meterRmsL { 0.0f }, meterRmsR { 0.0f };
};

} // namespace audiorack
