#pragma once

#include "../../core/ModuleRegistry.h"
#include "../common/SlidingWindow.h"

#include <atomic>

namespace audiorack
{

/** LM-1 — lookahead true-peak limiter. 4x oversampled detection, sliding-min
    plus cascaded-boxcar gain smoothing (no overshoot by construction),
    honest latency reporting. See docs/modules/limiter.md.
*/
class LimiterModule final : public AudioModule
{
public:
    static constexpr ModuleDescriptor kDescriptor { "lim", "Limiter", "Dynamics", 1 };

    static ModuleTypeInfo typeInfo();
    static void declareParameters (ParameterBuilder&);

    ModuleDescriptor descriptor() const override { return kDescriptor; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void bindParameter (const juce::String& idSuffix, std::atomic<float>* value) override;

    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept override;

    int  latencySamples() const noexcept override { return totalLatency.load (std::memory_order_relaxed); }
    void getMeterFrame (MeterFrame&) const noexcept override;

private:
    void applyLookahead() noexcept;   // recompute window sizes from the parameter

    struct Param
    {
        std::atomic<float>* value;
        explicit Param (float def) : value (&fallback), fallback (def) {}
        float get() const noexcept { return value->load (std::memory_order_relaxed); }
        std::atomic<float> fallback;
    };

    Param ceiling   { -0.3f };
    Param releaseMs { 100.0f };
    Param lookahead { 2.0f };

    double fs = 44100.0;
    int    channels = 2;
    int    maxLookaheadSamples = 0;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;  // detection path, mono
    juce::AudioBuffer<float> detectorBuffer;                      // mono |max| of inputs
    int oversamplerLatency = 0;

    dsp::DelayLine     mainDelay;
    dsp::RunningMin    runningMin;
    dsp::MovingAverage smootherA, smootherB;

    double releaseCoef  = 0.0;
    double cachedReleaseMs = -1.0;
    double releaseEnv   = 1.0;   // release-smoothed gain target
    float  cachedLookaheadMs = -1.0f;

    std::atomic<int>   totalLatency { 0 };
    std::atomic<float> meterGrDb { 0.0f };
    std::atomic<float> meterPeakL { 0.0f }, meterPeakR { 0.0f };
    std::atomic<float> meterRmsL { 0.0f }, meterRmsR { 0.0f };
};

} // namespace audiorack
