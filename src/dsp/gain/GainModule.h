#pragma once

#include "../../core/ModuleRegistry.h"

namespace audiorack
{

/** Utility gain, the M1 proof module: one smoothed gain parameter plus
    post-gain peak/RMS metering. See docs/modules/gain.md.
*/
class GainModule final : public AudioModule
{
public:
    static constexpr ModuleDescriptor kDescriptor { "gain", "Gain", "Utility", 1 };

    static ModuleTypeInfo typeInfo();
    static void declareParameters (ParameterBuilder&);

    // AudioModule
    ModuleDescriptor descriptor() const override { return kDescriptor; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void bindParameter (const juce::String& idSuffix, std::atomic<float>* value) override;

    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept override;
    void getMeterFrame (MeterFrame&) const noexcept override;

private:
    juce::SmoothedValue<float> gain;          // linear, 10 ms ramp

    std::atomic<float>* gainDb = &defaultGainDb;
    inline static std::atomic<float> defaultGainDb { 0.0f };

    // Channel mode: 0 Stereo (passthrough), 1 Mono (0.5·(L+R)), 2 Left (L→both),
    // 3 Right (R→both). Applied before the gain; Left/Right fix a mono source on
    // one input (e.g. a guitar on input 1) at full level.
    std::atomic<float>* chMode = &defaultChMode;
    inline static std::atomic<float> defaultChMode { 0.0f };

    // Written by process(), read by getMeterFrame() on the UI path.
    std::atomic<float> meterPeakL { 0.0f }, meterPeakR { 0.0f };
    std::atomic<float> meterRmsL  { 0.0f }, meterRmsR  { 0.0f };
};

} // namespace audiorack
