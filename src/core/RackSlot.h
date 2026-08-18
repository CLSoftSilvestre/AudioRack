#pragma once

#include "AudioModule.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace audiorack
{

/** One rack position: the mounted module plus per-slot bypass and wet/dry.

    `module` is written only by the audio thread (via the command queue), so
    process() needs no synchronisation to use it. Bypass and mix are read each
    block from atomics that M2 re-points at APVTS parameter storage; until
    then they fall back to slot-owned defaults (active, 100% wet).

    Bypass and mix collapse into a single smoothed wet gain, so engaging
    bypass is a 20 ms equal-gain crossfade to dry — click-free, and correct
    for correlated signals (linear, not equal-power).
*/
class RackSlot
{
public:
    void prepare (double sampleRate)
    {
        wetGain.reset (sampleRate, kRampSeconds);
        wetGain.setCurrentAndTargetValue (targetWetGain());
    }

    /// Audio thread: current wet target from the bound parameters.
    float targetWetGain() const noexcept
    {
        const bool bypassed = bypass->load (std::memory_order_relaxed) >= 0.5f;
        return bypassed ? 0.0f : mix->load (std::memory_order_relaxed);
    }

    void bindBypass (std::atomic<float>* p) noexcept { bypass = p != nullptr ? p : &defaultBypass; }
    void bindMix    (std::atomic<float>* p) noexcept { mix    = p != nullptr ? p : &defaultMix; }

    AudioModule* module = nullptr;                       // audio-thread view, owned via disposal queue
    juce::SmoothedValue<float> wetGain;

private:
    static constexpr double kRampSeconds = 0.02;

    std::atomic<float>* bypass = &defaultBypass;
    std::atomic<float>* mix    = &defaultMix;

    inline static std::atomic<float> defaultBypass { 0.0f };
    inline static std::atomic<float> defaultMix    { 1.0f };
};

} // namespace audiorack
