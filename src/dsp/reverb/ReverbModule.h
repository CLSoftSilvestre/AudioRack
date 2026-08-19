#pragma once

#include "../../core/ModuleRegistry.h"
#include "../common/Biquad.h"
#include "../common/FractionalDelay.h"

#include <array>
#include <atomic>

namespace audiorack
{

/** RV-8 — Feedback Delay Network reverb: 8 delay lines mixed by a normalised
    8x8 Hadamard matrix (lossless rotation), per-line damping low-pass,
    modulated line lengths for a lush tail, predelay and freeze. See
    docs/modules/reverb.md.
*/
class ReverbModule final : public AudioModule
{
public:
    static constexpr ModuleDescriptor kDescriptor { "reverb", "Reverb", "Time", 2 };
    static constexpr int kLines = 8;

    static ModuleTypeInfo typeInfo();
    static void declareParameters (ParameterBuilder&);

    ModuleDescriptor descriptor() const override { return kDescriptor; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void bindParameter (const juce::String& idSuffix, std::atomic<float>* value) override;

    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept override;
    void getMeterFrame (MeterFrame&) const noexcept override;

private:
    struct Param
    {
        std::atomic<float>* value;
        explicit Param (float def) : value (&fallback), fallback (def) {}
        float get() const noexcept { return value->load (std::memory_order_relaxed); }
        std::atomic<float> fallback;
    };

    Param size { 0.5f }, decay { 0.6f }, damping { 0.5f };
    Param predelayMs { 20.0f }, mix { 0.3f }, width { 1.0f }, freeze { 0.0f };

    double fs = 44100.0;

    dsp::FractionalDelay lines[kLines];
    dsp::Biquad          damp[kLines];
    dsp::FractionalDelay predelay[2];
    float                lineState[kLines] = {};
    double               baseDelay[kLines] = {};
    double               modPhase[kLines]  = {};

    juce::SmoothedValue<float> smoothedMix, smoothedDecay, smoothedSize;
    double cachedDamp = -1.0;

    std::atomic<float> meterPeakL { 0.0f }, meterPeakR { 0.0f };
    std::atomic<float> meterRmsL { 0.0f }, meterRmsR { 0.0f };
};

} // namespace audiorack
