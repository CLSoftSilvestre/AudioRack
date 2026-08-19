#pragma once

#include "../../core/ModuleRegistry.h"
#include "../common/Biquad.h"

#include <atomic>
#include <memory>
#include <vector>

namespace audiorack
{

/** AMP-1 — guitar amplifier with a cascaded tube preamp, passive-style tone
    stack, power-amp saturation and switchable analytic speaker cabinets
    (1x12 / 2x12 / 4x12). See docs/modules/amp.md.

    Three voicings (Clean / Crunch / Lead) select how many gain stages the
    signal cascades through. The nonlinear preamp and power stages run 4x
    oversampled (JUCE polyphase Oversampling); the tone stack sits between them
    at the oversampled rate, exactly where it does in a real amp. The cabinet is
    a fully synthetic filter model — no impulse-response files — applied at the
    base rate after decimation. Latency is the oversampler's reported FIR delay.
*/
class AmpModule final : public AudioModule
{
public:
    static constexpr ModuleDescriptor kDescriptor { "amp", "Guitar Amp", "Amp", 3 };

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
    static constexpr int kMaxStages = 3;
    static constexpr int kMaxCh     = 2;

    struct Param
    {
        std::atomic<float>* value;
        explicit Param (float def) : value (&fallback), fallback (def) {}
        float get() const noexcept { return value->load (std::memory_order_relaxed); }
        std::atomic<float> fallback;
    };

    // Knobs are 0..10 "amp dial" values; channel/cab are discrete indices.
    Param gain { 5.0f }, bass { 5.0f }, mid { 5.0f }, treble { 5.0f };
    Param presence { 5.0f }, master { 5.0f }, channel { 0.0f }, cab { 1.0f };

    // --- per-channel filter banks ---------------------------------------
    struct Preamp                     // one cascaded gain stage
    {
        dsp::Biquad couplingHp;       // interstage DC-blocking highpass
        dsp::Biquad brightLp;         // Miller/grid-stopper treble rolloff
    };
    Preamp      preamp[kMaxCh][kMaxStages];

    dsp::Biquad bassShelf[kMaxCh], midPeak[kMaxCh], trebleShelf[kMaxCh], presenceShelf[kMaxCh];

    dsp::Biquad cabHp[kMaxCh], cabLowRes[kMaxCh], cabScoop[kMaxCh], cabCone[kMaxCh], cabLp[kMaxCh];
    dsp::Biquad outDcBlock[kMaxCh];

    // Cabinet first-reflection comb (fixed short FIR: y = (x + a*x[n-D]) / (1+a)).
    std::vector<float> combBuf[kMaxCh];
    int combLen = 0, combWrite = 0;

    double fs   = 44100.0;
    double fsOs = 176400.0;            // oversampled rate
    int    ratio = 4;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    juce::SmoothedValue<float> smoothedIn, smoothedOut;

    std::atomic<int>   latency { 0 };
    std::atomic<float> meterPeakL { 0.0f }, meterPeakR { 0.0f };
    std::atomic<float> meterRmsL { 0.0f }, meterRmsR { 0.0f };

    void updateToneCoeffs() noexcept;
    void updateCabCoeffs() noexcept;
};

} // namespace audiorack
