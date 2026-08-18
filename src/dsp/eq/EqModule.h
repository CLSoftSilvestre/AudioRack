#pragma once

#include "../../core/ModuleRegistry.h"
#include "../common/Biquad.h"

#include <array>
#include <atomic>

namespace audiorack
{

/** EQ-6 — six-band parametric EQ (RBJ biquads): bell, low/high shelf,
    high-pass, low-pass per band, per-band enable and solo, output trim.
    The UI draws the live magnitude response from the same RBJ formulas.
    See docs/modules/eq.md.
*/
class EqModule final : public AudioModule
{
public:
    static constexpr ModuleDescriptor kDescriptor { "eq", "Parametric EQ", "EQ", 2 };
    static constexpr int kBands = 6;

    static ModuleTypeInfo typeInfo();
    static void declareParameters (ParameterBuilder&);

    ModuleDescriptor descriptor() const override { return kDescriptor; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void bindParameter (const juce::String& idSuffix, std::atomic<float>* value) override;

    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept override;
    void getMeterFrame (MeterFrame&) const noexcept override;

private:
    void refreshBand (int band) noexcept;

    struct Param
    {
        std::atomic<float>* value;
        explicit Param (float def) : value (&fallback), fallback (def) {}
        float get() const noexcept { return value->load (std::memory_order_relaxed); }
        std::atomic<float> fallback;
    };

    struct Band
    {
        Param type; Param freq; Param gain; Param q; Param on; Param solo;
        dsp::Biquad filter[2];                    // per channel
        float cachedType = -1.0f, cachedFreq = -1.0f, cachedGain = -999.0f, cachedQ = -1.0f;

        Band (float defFreq)
            : type (0.0f), freq (defFreq), gain (0.0f), q (0.71f), on (1.0f), solo (0.0f) {}
    };

    std::array<Band, kBands> bands { Band { 60.0f },  Band { 150.0f },  Band { 400.0f },
                                     Band { 1000.0f }, Band { 3500.0f }, Band { 10000.0f } };
    Param trim { 0.0f };

    double fs = 44100.0;
    juce::SmoothedValue<float> trimGain;

    std::atomic<float> meterPeakL { 0.0f }, meterPeakR { 0.0f };
    std::atomic<float> meterRmsL { 0.0f }, meterRmsR { 0.0f };
};

} // namespace audiorack
