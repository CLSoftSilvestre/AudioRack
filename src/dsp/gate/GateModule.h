#pragma once

#include "../../core/ModuleRegistry.h"
#include "../common/Biquad.h"
#include "../common/SlidingWindow.h"

#include <atomic>

namespace audiorack
{

/** GT-1 — noise gate with hysteresis, hold, range, sidechain HPF, external
    sidechain and lookahead. See docs/modules/gate.md.
*/
class GateModule final : public AudioModule
{
public:
    static constexpr ModuleDescriptor kDescriptor { "gate", "Gate", "Dynamics", 1 };

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

    Param threshold  { -40.0f }, hysteresis { 3.0f }, range { 80.0f };
    Param attack     { 0.5f },   hold { 50.0f },      releaseMs { 150.0f };
    Param scHpfHz    { 20.0f },  scSource { 0.0f },   lookaheadMs { 0.0f };

    enum class State { closed, open, holding };

    double fs = 44100.0;
    int    channels = 2;
    int    maxLookaheadSamples = 0;

    dsp::Biquad    scFilter;
    dsp::DelayLine mainDelay;

    double envState   = 0.0;   // fast detection envelope (linear)
    double envAttack  = 0.0, envRelease = 0.0;
    double gain       = 0.0;   // smoothed gate gain (linear)
    double attackCoef = 0.0, releaseCoef = 0.0;
    double cachedAttackMs = -1.0, cachedReleaseMs = -1.0, cachedScHpf = -1.0;
    float  cachedLookahead = -1.0f;
    int    holdCounter = 0;
    State  state = State::closed;

    std::atomic<int>   latency { 0 };
    std::atomic<float> meterGrDb { 0.0f };
    std::atomic<float> meterPeakL { 0.0f }, meterPeakR { 0.0f };
    std::atomic<float> meterRmsL { 0.0f }, meterRmsR { 0.0f };
};

} // namespace audiorack
