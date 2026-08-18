// Stereo delay.
//
// Digital mode: clean fractional delay, feedback with an optional tone LPF.
// Tape mode: the same line, but the read position is modulated by a
// wow/flutter LFO (slow ~0.6 Hz wow + faster ~6 Hz flutter) and the feedback
// path is always tone-filtered (tape head loss). Lagrange interpolation makes
// the modulated read glitch-free.
//
// Sync: when enabled the delay time is a note division of the host tempo,
// samples = (60/bpm) * beats * fs. Ping-pong cross-feeds: each channel's
// feedback is injected into the *other* line.
//
// Time changes are smoothed (20 ms) so automation and tempo edits glide
// rather than click; the Lagrange reader handles the sub-sample motion.

#include "DelayModule.h"

#include <cmath>

namespace audiorack
{

namespace
{
    constexpr double kMaxDelayMs = 4000.0;

    // Note divisions: index -> beats (quarter = 1.0). Matches the UI choices.
    constexpr double kDivisionBeats[] = {
        0.25,        // 1/16
        0.5,         // 1/8
        0.75,        // 1/8 dotted
        1.0 / 3.0,   // 1/8 triplet
        1.0,         // 1/4
        1.5,         // 1/4 dotted
        2.0 / 3.0,   // 1/4 triplet
        2.0,         // 1/2
        4.0          // 1/1
    };
    constexpr int kNumDivisions = static_cast<int> (std::size (kDivisionBeats));
}

ModuleTypeInfo DelayModule::typeInfo()
{
    return {
        kDescriptor,
        [] { return std::unique_ptr<AudioModule> (std::make_unique<DelayModule>()); },
        &DelayModule::declareParameters
    };
}

void DelayModule::declareParameters (ParameterBuilder& b)
{
    juce::NormalisableRange<float> timeRange (1.0f, static_cast<float> (kMaxDelayMs), 0.1f);
    timeRange.setSkewForCentre (350.0f);

    juce::NormalisableRange<float> toneRange (500.0f, 18000.0f, 1.0f);
    toneRange.setSkewForCentre (4000.0f);

    b.add ({ "time",     "Time",       timeRange, 350.0f, "ms" });
    b.add ({ "sync",     "Sync",       { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Free", "Sync" } });
    b.add ({ "division", "Division",   { 0.0f, static_cast<float> (kNumDivisions - 1), 1.0f }, 4.0f, "",
             { "1/16", "1/8", "1/8.", "1/8T", "1/4", "1/4.", "1/4T", "1/2", "1/1" } });
    b.add ({ "feedback", "Feedback",   { 0.0f, 1.1f, 0.001f }, 0.35f, "" });
    b.add ({ "mix",      "Mix",        { 0.0f, 1.0f, 0.001f }, 0.30f, "" });
    b.add ({ "mode",     "Mode",       { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Digital", "Tape" } });
    b.add ({ "pingpong", "Ping-Pong",  { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Off", "On" } });
    b.add ({ "tone",     "Tone",       toneRange, 6000.0f, "Hz" });
    b.add ({ "flutter",  "Wow/Flutter",{ 0.0f, 1.0f, 0.001f }, 0.20f, "" });
    b.add ({ "offset",   "Stereo",     { -50.0f, 50.0f, 0.1f }, 0.0f, "ms" });
}

void DelayModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;

    const double maxSamples = kMaxDelayMs * 0.001 * fs + 64.0;

    for (int ch = 0; ch < kChannels; ++ch)
    {
        lines[ch].prepare (maxSamples);
        smoothedDelay[ch].reset (fs, 0.02);
    }

    smoothedFeedback.reset (fs, 0.02);
    smoothedMix.reset (fs, 0.02);
    cachedTone = -1.0;
    reset();
}

void DelayModule::reset()
{
    for (int ch = 0; ch < kChannels; ++ch)
    {
        lines[ch].reset();
        feedbackTone[ch].reset();
        feedbackState[ch] = 0.0f;
        smoothedDelay[ch].setCurrentAndTargetValue (static_cast<float> (targetDelaySamples (ch, {})));
    }
    lfoPhase = 0.0;
    meterPeakL.store (0.0f, std::memory_order_relaxed);
    meterPeakR.store (0.0f, std::memory_order_relaxed);
}

void DelayModule::bindParameter (const juce::String& id, std::atomic<float>* v)
{
    if (v == nullptr) return;

    if      (id == "time")     timeMs.value = v;
    else if (id == "sync")     sync.value = v;
    else if (id == "division") division.value = v;
    else if (id == "feedback") feedback.value = v;
    else if (id == "mix")      mix.value = v;
    else if (id == "mode")     mode.value = v;
    else if (id == "pingpong") pingpong.value = v;
    else if (id == "tone")     toneHz.value = v;
    else if (id == "flutter")  flutter.value = v;
    else if (id == "offset")   stereoOffset.value = v;
}

double DelayModule::targetDelaySamples (int channel, const ProcessContext& ctx) const noexcept
{
    double ms;

    if (sync.get() >= 0.5f)
    {
        const int idx = juce::jlimit (0, kNumDivisions - 1, static_cast<int> (division.get()));
        const double bpm = ctx.transport.bpm > 1.0 ? ctx.transport.bpm : 120.0;
        ms = (60000.0 / bpm) * kDivisionBeats[idx];
    }
    else
    {
        ms = static_cast<double> (timeMs.get());
    }

    // Right channel offset (stereo widening / slapback).
    if (channel == 1)
        ms += static_cast<double> (stereoOffset.get());

    ms = juce::jlimit (1.0, kMaxDelayMs, ms);
    return ms * 0.001 * fs;
}

void DelayModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& ctx) noexcept
{
    const auto numSamples  = static_cast<int> (block.getNumSamples());
    const auto numChannels = static_cast<int> (block.getNumChannels());

    const bool tape     = mode.get() >= 0.5f;
    const bool pingPong = pingpong.get() >= 0.5f;
    const float flutterAmt = flutter.get();

    // Feedback tone filter (feedback path LPF).
    const double tone = static_cast<double> (toneHz.get());
    if (! juce::exactlyEqual (tone, cachedTone))
    {
        for (auto& f : feedbackTone)
            f.setLowpass (tone, 0.707, fs);
        cachedTone = tone;
    }

    for (int ch = 0; ch < kChannels; ++ch)
        smoothedDelay[ch].setTargetValue (static_cast<float> (targetDelaySamples (ch, ctx)));
    smoothedFeedback.setTargetValue (juce::jlimit (0.0f, 1.1f, feedback.get()));
    smoothedMix.setTargetValue (mix.get());

    // Flutter LFO: wow (0.6 Hz) + flutter (6.3 Hz), depth up to ~4 ms scaled.
    const double wowInc     = 2.0 * juce::MathConstants<double>::pi * 0.6  / fs;
    const double flutterInc = 2.0 * juce::MathConstants<double>::pi * 6.3  / fs;
    const double maxModSamples = 0.004 * fs;

    float peak[2] = { 0.0f, 0.0f };
    double sumSq[2] = { 0.0, 0.0 };

    for (int i = 0; i < numSamples; ++i)
    {
        const float fb  = smoothedFeedback.getNextValue();
        const float wet = smoothedMix.getNextValue();
        const float dry = 1.0f - wet;

        lfoPhase += 1.0;
        double mod = 0.0;
        if (tape)
            mod = flutterAmt * maxModSamples
                  * (0.7 * std::sin (lfoPhase * wowInc) + 0.3 * std::sin (lfoPhase * flutterInc));

        float delayedOut[kChannels] = { 0.0f, 0.0f };

        for (int ch = 0; ch < numChannels && ch < kChannels; ++ch)
        {
            const double delaySamp = static_cast<double> (smoothedDelay[ch].getNextValue()) + mod;
            delayedOut[ch] = lines[ch].read (delaySamp);
        }
        // Advance the single-channel smoothers even if input is mono.
        for (int ch = numChannels; ch < kChannels; ++ch)
            smoothedDelay[ch].getNextValue();

        for (int ch = 0; ch < numChannels && ch < kChannels; ++ch)
        {
            auto* data = block.getChannelPointer (static_cast<size_t> (ch));
            const float input = data[i];

            // Feedback source: ping-pong reads the opposite channel's output.
            const int fbSource = pingPong ? (ch ^ 1) : ch;
            float fbSignal = delayedOut[fbSource] * fb;

            if (tape)
                fbSignal = feedbackTone[ch].process (fbSignal);

            // Soft-clip runaway feedback (tape saturation ceiling).
            fbSignal = std::tanh (fbSignal);

            lines[ch].write (input + fbSignal);

            const float out = dry * input + wet * delayedOut[ch];
            data[i] = out;

            peak[static_cast<size_t> (ch)] = juce::jmax (peak[static_cast<size_t> (ch)], std::abs (out));
            sumSq[static_cast<size_t> (ch)] += static_cast<double> (out) * static_cast<double> (out);
        }
    }

    meterPeakL.store (peak[0], std::memory_order_relaxed);
    meterPeakR.store (peak[1], std::memory_order_relaxed);
    const auto n = static_cast<double> (juce::jmax (1, numSamples));
    meterRmsL.store (static_cast<float> (std::sqrt (sumSq[0] / n)), std::memory_order_relaxed);
    meterRmsR.store (static_cast<float> (std::sqrt (sumSq[1] / n)), std::memory_order_relaxed);
}

void DelayModule::getMeterFrame (MeterFrame& frame) const noexcept
{
    frame.peakL = meterPeakL.load (std::memory_order_relaxed);
    frame.peakR = meterPeakR.load (std::memory_order_relaxed);
    frame.rmsL  = meterRmsL.load (std::memory_order_relaxed);
    frame.rmsR  = meterRmsR.load (std::memory_order_relaxed);
    frame.gainReductionDb = 0.0f;
}

} // namespace audiorack
