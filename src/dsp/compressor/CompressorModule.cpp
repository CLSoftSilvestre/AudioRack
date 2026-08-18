// Feed-forward / feed-back dynamic range compressor.
//
// Reference: D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range
// Compressor Design — A Tutorial and Analysis", JAES 60(6), 2012.
//
//   level      L[n]  = 20 log10 |sc[n]|          (peak)  or via 5 ms RMS of sc^2
//   computer   G(L)  : below knee -> L ; inside |2(L-T)| <= W ->
//              L + (1/R - 1)(L - T + W/2)^2 / (2W) ; above -> T + (L-T)/R
//   raw GR     g[n]  = L[n] - G(L[n])  >= 0  (dB)
//   smoothing  ("smooth decoupled peak detector", eq. 17):
//              yR[n] = max(g[n], aR yR[n-1] + (1-aR) g[n])
//              gr[n] = aA gr[n-1] + (1-aA) yR[n]
//   with a = exp(-1/(tau fs)). Program-dependent release scales tau_R by the
//   ~1 s average GR: tau_eff = tau_R * clamp(0.25 + grAvg/6, 0.25, 4) — short
//   peaks recover fast, sustained compression releases slowly.
//   Auto-makeup = -G(0)/2: half the reduction a full-scale signal would get.

#include "CompressorModule.h"

#include <cmath>

namespace audiorack
{

namespace
{
    constexpr float  kSilenceDb  = -120.0f;
    constexpr double kRmsMs      = 5.0;
    constexpr double kLongTermMs = 1000.0;

    float toDb (float linear) noexcept
    {
        return linear <= 1.0e-6f ? kSilenceDb : 20.0f * std::log10 (linear);
    }

    float fromDb (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }
} // namespace

ModuleTypeInfo CompressorModule::typeInfo()
{
    return {
        kDescriptor,
        [] { return std::unique_ptr<AudioModule> (std::make_unique<CompressorModule>()); },
        &CompressorModule::declareParameters
    };
}

void CompressorModule::declareParameters (ParameterBuilder& b)
{
    auto skewed = [] (float min, float max, float centre)
    {
        juce::NormalisableRange<float> r (min, max, 0.01f);
        r.setSkewForCentre (centre);
        return r;
    };

    b.add ({ "threshold", "Threshold", { -60.0f, 0.0f, 0.1f }, -18.0f, "dB" });
    b.add ({ "ratio",     "Ratio",     skewed (1.0f, 20.0f, 4.0f), 4.0f, ":1" });
    b.add ({ "knee",      "Knee",      { 0.0f, 24.0f, 0.1f }, 6.0f, "dB" });
    b.add ({ "attack",    "Attack",    skewed (0.05f, 100.0f, 10.0f), 10.0f, "ms" });
    b.add ({ "release",   "Release",   skewed (5.0f, 2000.0f, 150.0f), 150.0f, "ms" });
    b.add ({ "makeup",    "Makeup",    { 0.0f, 24.0f, 0.1f }, 0.0f, "dB" });
    b.add ({ "automakeup","Makeup Mode", { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Manual", "Auto" } });
    b.add ({ "topology",  "Topology",  { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Feed-Forward", "Feed-Back" } });
    b.add ({ "detector",  "Detector",  { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Peak", "RMS" } });
    b.add ({ "pdr",       "Release Mode", { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Fixed", "Program" } });
    b.add ({ "schpf",     "SC HPF",    skewed (20.0f, 500.0f, 100.0f), 20.0f, "Hz" });
    b.add ({ "scsource",  "SC Source", { 0.0f, 1.0f, 1.0f }, 0.0f, "", { "Internal", "External" } });
}

void CompressorModule::prepare (double sampleRate, int, int)
{
    fs = sampleRate;
    rmsCoef        = std::exp (-1.0 / (kRmsMs * 0.001 * fs));
    grLongTermCoef = std::exp (-1.0 / (kLongTermMs * 0.001 * fs));
    cachedAttackMs = cachedReleaseMs = cachedScHpf = -1.0;
    updateCoefficients();
    reset();
}

void CompressorModule::reset()
{
    scFilter.reset();
    rmsState = releaseState = grSmoothed = grLongTerm = 0.0;
    prevOutputAbs = 0.0f;
    meterGrDb.store (0.0f, std::memory_order_relaxed);
}

void CompressorModule::bindParameter (const juce::String& id, std::atomic<float>* v)
{
    if (v == nullptr) return;

    if      (id == "threshold")  threshold.value = v;
    else if (id == "ratio")      ratio.value = v;
    else if (id == "knee")       knee.value = v;
    else if (id == "attack")     attack.value = v;
    else if (id == "release")    release.value = v;
    else if (id == "makeup")     makeup.value = v;
    else if (id == "automakeup") autoMakeup.value = v;
    else if (id == "topology")   topology.value = v;
    else if (id == "detector")   detectorMode.value = v;
    else if (id == "pdr")        programRelease.value = v;
    else if (id == "schpf")      scHpfHz.value = v;
    else if (id == "scsource")   scSource.value = v;
}

float CompressorModule::gainComputerDb (float L, float T, float R, float W) const noexcept
{
    const float overshoot = L - T;

    if (2.0f * overshoot < -W)
        return L;

    if (2.0f * std::abs (overshoot) <= W)
    {
        const float x = overshoot + W * 0.5f;
        return L + (1.0f / R - 1.0f) * x * x / (2.0f * W);
    }

    return T + overshoot / R;
}

void CompressorModule::updateCoefficients() noexcept
{
    const double attackMs = static_cast<double> (attack.get());

    double releaseMs = static_cast<double> (release.get());

    if (programRelease.get() >= 0.5f)
    {
        const double scale = juce::jlimit (0.25, 4.0, 0.25 + grLongTerm / 6.0);
        releaseMs *= scale;
    }

    if (! juce::exactlyEqual (attackMs, cachedAttackMs))
    {
        attackCoef = std::exp (-1.0 / (attackMs * 0.001 * fs));
        cachedAttackMs = attackMs;
    }

    if (! juce::exactlyEqual (releaseMs, cachedReleaseMs))
    {
        releaseCoef = std::exp (-1.0 / (releaseMs * 0.001 * fs));
        cachedReleaseMs = releaseMs;
    }

    const double hpf = static_cast<double> (scHpfHz.get());

    if (! juce::exactlyEqual (hpf, cachedScHpf))
    {
        scFilter.setHighpass (hpf, 0.707, fs);
        cachedScHpf = hpf;
    }
}

void CompressorModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& ctx) noexcept
{
    updateCoefficients();

    const auto numSamples  = static_cast<int> (block.getNumSamples());
    const auto numChannels = static_cast<int> (block.getNumChannels());

    const float T = threshold.get();
    const float R = juce::jmax (1.0f, ratio.get());
    const float W = juce::jmax (0.01f, knee.get());

    const bool feedback = topology.get() >= 0.5f;
    const bool rmsMode  = detectorMode.get() >= 0.5f;
    const bool external = scSource.get() >= 0.5f && ctx.sidechain != nullptr
                          && ctx.numSidechainChannels > 0;

    float makeupDb = makeup.get();
    if (autoMakeup.get() >= 0.5f)
        makeupDb = -gainComputerDb (0.0f, T, R, W) * 0.5f;
    const float makeupLin = fromDb (makeupDb);

    float blockMaxGr = 0.0f;
    float peak[2] = { 0.0f, 0.0f };
    double sumSq[2] = { 0.0, 0.0 };

    for (int i = 0; i < numSamples; ++i)
    {
        // --- detector source: mono mix -----------------------------------
        float sc;

        if (external && i < ctx.numSidechainSamples)
        {
            float mix = 0.0f;
            for (int ch = 0; ch < ctx.numSidechainChannels; ++ch)
                mix += ctx.sidechain[ch][i];
            sc = mix / static_cast<float> (ctx.numSidechainChannels);
        }
        else if (feedback)
        {
            sc = prevOutputAbs;   // rectified already
        }
        else
        {
            float mix = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                mix += block.getChannelPointer (static_cast<size_t> (ch))[i];
            sc = mix / static_cast<float> (numChannels);
        }

        if (! feedback)
            sc = scFilter.process (sc);

        const float rect = std::abs (sc);

        // --- level in dB ---------------------------------------------------
        float levelDb;

        if (rmsMode)
        {
            rmsState = rmsCoef * rmsState
                     + (1.0 - rmsCoef) * static_cast<double> (rect) * static_cast<double> (rect);
            levelDb = toDb (static_cast<float> (std::sqrt (rmsState)));
        }
        else
        {
            levelDb = toDb (rect);
        }

        // --- gain computer + decoupled smoothing ---------------------------
        const float grRaw = juce::jmax (0.0f, levelDb - gainComputerDb (levelDb, T, R, W));

        releaseState = juce::jmax (static_cast<double> (grRaw),
                                   releaseCoef * releaseState
                                     + (1.0 - releaseCoef) * static_cast<double> (grRaw));
        grSmoothed = attackCoef * grSmoothed + (1.0 - attackCoef) * releaseState;

        grLongTerm = grLongTermCoef * grLongTerm + (1.0 - grLongTermCoef) * grSmoothed;

        const float grDb = static_cast<float> (grSmoothed);
        const float gain = fromDb (-grDb) * makeupLin;

        blockMaxGr = juce::jmax (blockMaxGr, grDb);

        // --- apply ---------------------------------------------------------
        float outAbsMix = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (static_cast<size_t> (ch));
            const float out = data[i] * gain;
            data[i] = out;

            outAbsMix += out;

            if (ch < 2)
            {
                peak[ch] = juce::jmax (peak[ch], std::abs (out));
                sumSq[ch] += static_cast<double> (out) * static_cast<double> (out);
            }
        }

        prevOutputAbs = std::abs (outAbsMix / static_cast<float> (numChannels));
    }

    meterGrDb.store (blockMaxGr, std::memory_order_relaxed);
    meterPeakL.store (peak[0], std::memory_order_relaxed);
    meterPeakR.store (peak[1], std::memory_order_relaxed);

    const auto n = static_cast<double> (juce::jmax (1, numSamples));
    meterRmsL.store (static_cast<float> (std::sqrt (sumSq[0] / n)), std::memory_order_relaxed);
    meterRmsR.store (static_cast<float> (std::sqrt (sumSq[1] / n)), std::memory_order_relaxed);
}

void CompressorModule::getMeterFrame (MeterFrame& frame) const noexcept
{
    frame.peakL = meterPeakL.load (std::memory_order_relaxed);
    frame.peakR = meterPeakR.load (std::memory_order_relaxed);
    frame.rmsL  = meterRmsL.load (std::memory_order_relaxed);
    frame.rmsR  = meterRmsR.load (std::memory_order_relaxed);
    frame.gainReductionDb = meterGrDb.load (std::memory_order_relaxed);
}

} // namespace audiorack
