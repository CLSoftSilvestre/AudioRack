#pragma once

#include "../../core/ModuleRegistry.h"
#include "../common/Biquad.h"

#include <atomic>

namespace audiorack
{

/** CMP-2 — feed-forward / feed-back compressor with soft knee, peak/RMS
    detection, program-dependent release, sidechain HPF, external sidechain
    and auto-makeup. See docs/modules/compressor.md for the maths and
    references (Giannoulis/Massberg/Reiss 2012).
*/
class CompressorModule final : public AudioModule
{
public:
    static constexpr ModuleDescriptor kDescriptor { "comp", "Compressor", "Dynamics", 2 };

    static ModuleTypeInfo typeInfo();
    static void declareParameters (ParameterBuilder&);

    ModuleDescriptor descriptor() const override { return kDescriptor; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void bindParameter (const juce::String& idSuffix, std::atomic<float>* value) override;

    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept override;
    void getMeterFrame (MeterFrame&) const noexcept override;

private:
    float gainComputerDb (float levelDb, float thresholdDb, float ratio, float kneeDb) const noexcept;
    void  updateCoefficients() noexcept;

    // Parameter bindings (APVTS atomics; defaults used when unbound in tests)
    struct Param
    {
        std::atomic<float>* value;
        explicit Param (float def) : value (&fallback), fallback (def) {}
        float get() const noexcept { return value->load (std::memory_order_relaxed); }
        std::atomic<float> fallback;
    };

    Param threshold  { -18.0f },  ratio { 4.0f },    knee { 6.0f };
    Param attack     { 10.0f },   release { 150.0f };
    Param makeup     { 0.0f },    autoMakeup { 0.0f };
    Param topology   { 0.0f },    detectorMode { 0.0f }, programRelease { 0.0f };
    Param scHpfHz    { 20.0f },   scSource { 0.0f };

    // State
    double fs = 44100.0;

    dsp::Biquad scFilter;                 // on the mono detector signal
    double rmsState       = 0.0;          // fixed 5 ms RMS averager (of x^2)
    double rmsCoef        = 0.0;
    double releaseState   = 0.0;          // decoupled release stage (dB of GR)
    double grSmoothed     = 0.0;          // attack-smoothed GR (dB)
    double grLongTerm     = 0.0;          // ~1 s average GR, drives program release
    double grLongTermCoef = 0.0;
    float  prevOutputAbs  = 0.0f;         // feed-back topology detector source

    // Cached per-block coefficients
    double attackCoef = 0.0, releaseCoef = 0.0;
    double cachedAttackMs = -1.0, cachedReleaseMs = -1.0, cachedScHpf = -1.0;

    // Meters
    std::atomic<float> meterGrDb { 0.0f };
    std::atomic<float> meterPeakL { 0.0f }, meterPeakR { 0.0f };
    std::atomic<float> meterRmsL { 0.0f }, meterRmsR { 0.0f };
};

} // namespace audiorack
