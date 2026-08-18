// Noise gate with hysteresis (Reiss & McPherson, "Audio Effects" ch. 6).
//
//   Detection: mono sidechain (internal input or external bus) -> RBJ HPF ->
//   |x| -> fast envelope (0.05 ms attack / 5 ms release one-pole), so the
//   state machine sees transients instantly but doesn't chatter on waveform
//   ripple.
//
//   State machine with hysteresis: opens when env > T, arms hold when
//   env < T - H (the hysteresis window prevents chatter around threshold),
//   counts down hold, then releases towards the floor.
//
//   Gain: one-pole towards 1.0 (attack coef) or towards 10^(-range/20)
//   (release coef). Lookahead delays the *audio* only, so the detector leads
//   the programme and the gate is already opening when the transient arrives;
//   the delay is reported as latency.

#include "GateModule.h"

#include <cmath>

namespace audiorack
{

namespace
{
    constexpr double kMaxLookaheadMs = 10.0;
    constexpr double kEnvAttackMs    = 0.05;
    constexpr double kEnvReleaseMs   = 5.0;

    float fromDb (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
}

ModuleTypeInfo GateModule::typeInfo()
{
    return {
        kDescriptor,
        [] { return std::unique_ptr<AudioModule> (std::make_unique<GateModule>()); },
        &GateModule::declareParameters
    };
}

void GateModule::declareParameters (ParameterBuilder& b)
{
    auto skewed = [] (float min, float max, float centre)
    {
        juce::NormalisableRange<float> r (min, max, 0.01f);
        r.setSkewForCentre (centre);
        return r;
    };

    b.add ({ "threshold",  "Threshold",  { -80.0f, 0.0f, 0.1f }, -40.0f, "dB" });
    b.add ({ "hysteresis", "Hysteresis", { 0.0f, 24.0f, 0.1f }, 3.0f, "dB" });
    b.add ({ "attack",     "Attack",     skewed (0.01f, 50.0f, 1.0f), 0.5f, "ms" });
    b.add ({ "hold",       "Hold",       { 0.0f, 500.0f, 1.0f }, 50.0f, "ms" });
    b.add ({ "release",    "Release",    skewed (5.0f, 4000.0f, 200.0f), 150.0f, "ms" });
    b.add ({ "range",      "Range",      { 0.0f, 90.0f, 0.5f }, 80.0f, "dB" });
    b.add ({ "schpf",      "SC HPF",     skewed (20.0f, 2000.0f, 200.0f), 20.0f, "Hz" });
    b.add ({ "scsource",   "SC Source",  { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Internal", "External" } });
    b.add ({ "lookahead",  "Lookahead",  { 0.0f, 10.0f, 0.1f }, 0.0f, "ms" });
}

void GateModule::prepare (double sampleRate, int, int numChannels)
{
    fs       = sampleRate;
    channels = numChannels;

    maxLookaheadSamples = static_cast<int> (std::ceil (kMaxLookaheadMs * 0.001 * fs)) + 1;
    mainDelay.prepare (maxLookaheadSamples, channels);

    envAttack  = std::exp (-1.0 / (kEnvAttackMs * 0.001 * fs));
    envRelease = std::exp (-1.0 / (kEnvReleaseMs * 0.001 * fs));

    cachedAttackMs = cachedReleaseMs = cachedScHpf = -1.0;
    cachedLookahead = -1.0f;
    reset();
}

void GateModule::reset()
{
    scFilter.reset();
    mainDelay.reset();
    envState = 0.0;
    gain = static_cast<double> (fromDb (-range.get()));
    holdCounter = 0;
    state = State::closed;
    meterGrDb.store (0.0f, std::memory_order_relaxed);
}

void GateModule::bindParameter (const juce::String& id, std::atomic<float>* v)
{
    if (v == nullptr) return;

    if      (id == "threshold")  threshold.value = v;
    else if (id == "hysteresis") hysteresis.value = v;
    else if (id == "attack")     attack.value = v;
    else if (id == "hold")       hold.value = v;
    else if (id == "release")    releaseMs.value = v;
    else if (id == "range")      range.value = v;
    else if (id == "schpf")      scHpfHz.value = v;
    else if (id == "scsource")   scSource.value = v;
    else if (id == "lookahead")  lookaheadMs.value = v;
}

void GateModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& ctx) noexcept
{
    // --- per-block parameter/coefficient refresh --------------------------
    const double attackParam  = static_cast<double> (attack.get());
    const double releaseParam = static_cast<double> (releaseMs.get());
    const double hpfParam     = static_cast<double> (scHpfHz.get());
    const float  lookParam    = lookaheadMs.get();

    if (! juce::exactlyEqual (attackParam, cachedAttackMs))
    {
        attackCoef = std::exp (-1.0 / (attackParam * 0.001 * fs));
        cachedAttackMs = attackParam;
    }
    if (! juce::exactlyEqual (releaseParam, cachedReleaseMs))
    {
        releaseCoef = std::exp (-1.0 / (releaseParam * 0.001 * fs));
        cachedReleaseMs = releaseParam;
    }
    if (! juce::exactlyEqual (hpfParam, cachedScHpf))
    {
        scFilter.setHighpass (hpfParam, 0.707, fs);
        cachedScHpf = hpfParam;
    }
    if (! juce::exactlyEqual (lookParam, cachedLookahead))
    {
        cachedLookahead = lookParam;
        const int la = juce::jlimit (0, maxLookaheadSamples,
                                     static_cast<int> (std::round (static_cast<double> (lookParam) * 0.001 * fs)));
        mainDelay.setDelay (la);
        latency.store (la, std::memory_order_relaxed);
    }

    const auto numSamples  = static_cast<int> (block.getNumSamples());
    const auto numChannels = static_cast<int> (block.getNumChannels());

    const float openLin  = fromDb (threshold.get());
    const float closeLin = fromDb (threshold.get() - hysteresis.get());
    const float floorLin = fromDb (-range.get());
    const int   holdSamples = static_cast<int> (static_cast<double> (hold.get()) * 0.001 * fs);

    const bool external = scSource.get() >= 0.5f && ctx.sidechain != nullptr
                          && ctx.numSidechainChannels > 0;

    float blockMaxGr = 0.0f;
    float peak[2] = { 0.0f, 0.0f };
    double sumSq[2] = { 0.0, 0.0 };

    for (int i = 0; i < numSamples; ++i)
    {
        // --- detector ----------------------------------------------------
        float sc;

        if (external && i < ctx.numSidechainSamples)
        {
            float mix = 0.0f;
            for (int ch = 0; ch < ctx.numSidechainChannels; ++ch)
                mix += ctx.sidechain[ch][i];
            sc = mix / static_cast<float> (ctx.numSidechainChannels);
        }
        else
        {
            float mix = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                mix += block.getChannelPointer (static_cast<size_t> (ch))[i];
            sc = mix / static_cast<float> (numChannels);
        }

        const float rect = std::abs (scFilter.process (sc));
        const double coef = static_cast<double> (rect) > envState ? envAttack : envRelease;
        envState = coef * envState + (1.0 - coef) * static_cast<double> (rect);
        const float env = static_cast<float> (envState);

        // --- state machine with hysteresis ---------------------------------
        switch (state)
        {
            case State::closed:
                if (env > openLin) state = State::open;
                break;

            case State::open:
                if (env < closeLin)
                {
                    state = State::holding;
                    holdCounter = holdSamples;
                }
                break;

            case State::holding:
                if (env > openLin)
                    state = State::open;
                else if (--holdCounter <= 0)
                    state = State::closed;
                break;
        }

        // --- gain smoothing -----------------------------------------------
        const double target = (state == State::closed) ? static_cast<double> (floorLin) : 1.0;
        const double gCoef  = target > gain ? attackCoef : releaseCoef;
        gain = gCoef * gain + (1.0 - gCoef) * target;

        const float g = static_cast<float> (gain);
        blockMaxGr = juce::jmax (blockMaxGr, -20.0f * std::log10 (juce::jmax (1.0e-5f, g)));

        // --- apply to (lookahead-delayed) audio -------------------------------
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (static_cast<size_t> (ch));
            const float out = mainDelay.process (ch, data[i]) * g;
            data[i] = out;

            if (ch < 2)
            {
                peak[static_cast<size_t> (ch)] = juce::jmax (peak[static_cast<size_t> (ch)], std::abs (out));
                sumSq[static_cast<size_t> (ch)] += static_cast<double> (out) * static_cast<double> (out);
            }
        }

        mainDelay.advance();
    }

    meterGrDb.store (blockMaxGr, std::memory_order_relaxed);
    meterPeakL.store (peak[0], std::memory_order_relaxed);
    meterPeakR.store (peak[1], std::memory_order_relaxed);

    const auto n = static_cast<double> (juce::jmax (1, numSamples));
    meterRmsL.store (static_cast<float> (std::sqrt (sumSq[0] / n)), std::memory_order_relaxed);
    meterRmsR.store (static_cast<float> (std::sqrt (sumSq[1] / n)), std::memory_order_relaxed);
}

void GateModule::getMeterFrame (MeterFrame& frame) const noexcept
{
    frame.peakL = meterPeakL.load (std::memory_order_relaxed);
    frame.peakR = meterPeakR.load (std::memory_order_relaxed);
    frame.rmsL  = meterRmsL.load (std::memory_order_relaxed);
    frame.rmsR  = meterRmsR.load (std::memory_order_relaxed);
    frame.gainReductionDb = meterGrDb.load (std::memory_order_relaxed);
}

} // namespace audiorack
