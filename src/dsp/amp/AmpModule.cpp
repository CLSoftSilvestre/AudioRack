// Guitar amplifier model.
//
// References:
//   D.T. Yeh, J.S. Abel, J.O. Smith, "Simulation of the diode limiter in guitar
//     distortion circuits by numerical solution of ordinary differential
//     equations", DAFx 2007 (tube/diode nonlinearity, oversampling motivation).
//   D.T. Yeh & J.O. Smith, "Discretization of the '59 Fender Bassman tone
//     stack", DAFx 2006 (the passive tone network we approximate with shelves).
//   R. Kuehnel, "Guitar Amplifier Circuit Analysis" (gain staging, coupling
//     capacitors, presence via the power-amp negative-feedback loop).
//   J.O. Smith, "Physical Audio Signal Processing" (speaker cabinet as a
//     resonant band-limited system: low-frequency box resonance, cone break-up
//     peak, steep top-end roll-off, mic-position comb).
//
// Signal chain (per channel):
//   input gain -> [N cascaded tube stages: asymmetric shaper -> coupling HP ->
//     bright LP -> interstage gain] -> tone stack (bass/mid/treble) -> presence
//     -> power-amp saturation        <-- all of the above 4x oversampled
//   -> cabinet (HP -> box resonance -> scoop -> cone peak -> LP -> comb)
//   -> DC block -> master/output     <-- base rate
//
// The nonlinear stages are oversampled so the harmonics they synthesise above
// fs/2 are created in the oversampled domain and filtered before decimation.
// The tone stack sits inside the oversampled region, between preamp and power
// amp, matching where it sits in the real circuit. The cabinet is linear, so it
// runs once at the base rate. There are no impulse-response files: every cab is
// a handful of biquads whose corner frequencies and resonances are chosen to
// match the measured behaviour of open/closed-back 12" speaker enclosures.

#include "AmpModule.h"

#include <cmath>

namespace audiorack
{

namespace
{
    float fromDb (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    // Asymmetric triode transfer. The positive half (grid driven up) saturates
    // sooner than the negative half, so the stage generates 2nd-harmonic
    // (even) content on top of the odd harmonics — the characteristic tube
    // "warmth". Small-signal slope is ~1 so cascaded gain is set explicitly.
    inline float tubeStage (float x) noexcept
    {
        if (x >= 0.0f)
            return std::tanh (x);
        return 0.75f * std::tanh (x / 0.75f);   // stiffer negative half
    }

    struct ChannelVoicing
    {
        int   stages;
        float driveBaseDb, driveRangeDb;   // gain knob 0..10 maps into this window
        float interStage;                  // linear gain between cascaded stages
        float makeupDb;                    // level trim so channels roughly match
    };

    // Clean: one nearly-linear stage. Crunch: two stages into mild clip.
    // Lead: three stages, the last two hard into saturation.
    constexpr ChannelVoicing kVoicings[3] = {
        { 1, -6.0f, 24.0f, 1.0f,   0.0f },   // Clean
        { 2,  0.0f, 30.0f, 3.2f,  -6.0f },   // Crunch
        { 3,  8.0f, 34.0f, 3.5f, -12.0f },   // Lead
    };

    struct CabVoicing
    {
        double hpHz;                       // sub-resonance rumble cut
        double resHz, resDb, resQ;         // box / low-frequency resonance
        double scoopHz, scoopDb, scoopQ;   // low-mid scoop (0 dB = none)
        double coneHz, coneDb, coneQ;      // cone break-up presence peak
        double lpHz;                       // top-end roll-off
        double combMs, combMix;            // first-reflection comb
    };

    // 1x12 open-back: tight, bright, light low end.
    // 2x12: fuller, a touch darker.
    // 4x12 closed-back: big low resonance, scooped low-mids, dark top.
    constexpr CabVoicing kCabs[3] = {
        { 85.0,  100.0, 3.0, 1.2,   0.0,  0.0, 1.0,  2200.0, 2.5, 1.3,  5200.0, 0.22, 0.14 },
        { 78.0,   88.0, 4.0, 1.1,   0.0,  0.0, 1.0,  1900.0, 2.0, 1.1,  4800.0, 0.28, 0.16 },
        { 72.0,   78.0, 5.0, 1.0, 550.0, -2.5, 0.9,  2600.0, 3.0, 1.4,  4200.0, 0.34, 0.18 },
    };
}

ModuleTypeInfo AmpModule::typeInfo()
{
    return {
        kDescriptor,
        [] { return std::unique_ptr<AudioModule> (std::make_unique<AmpModule>()); },
        &AmpModule::declareParameters
    };
}

void AmpModule::declareParameters (ParameterBuilder& b)
{
    b.add ({ "channel",  "Channel",  { 0.0f, 2.0f, 1.0f }, 0.0f, "", { "Clean", "Crunch", "Lead" } });
    b.add ({ "gain",     "Gain",     { 0.0f, 10.0f, 0.01f }, 5.0f, "" });
    b.add ({ "bass",     "Bass",     { 0.0f, 10.0f, 0.01f }, 5.0f, "" });
    b.add ({ "mid",      "Middle",   { 0.0f, 10.0f, 0.01f }, 5.0f, "" });
    b.add ({ "treble",   "Treble",   { 0.0f, 10.0f, 0.01f }, 5.0f, "" });
    b.add ({ "presence", "Presence", { 0.0f, 10.0f, 0.01f }, 5.0f, "" });
    b.add ({ "master",   "Master",   { 0.0f, 10.0f, 0.01f }, 5.0f, "" });
    b.add ({ "cab",      "Cabinet",  { 0.0f, 2.0f, 1.0f }, 1.0f, "", { "1x12", "2x12", "4x12" } });
}

void AmpModule::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    fs = sampleRate;

    const int ch = juce::jlimit (1, kMaxCh, numChannels);

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        static_cast<size_t> (ch),
        2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing (static_cast<size_t> (maxBlockSize));
    latency.store (static_cast<int> (std::ceil (oversampler->getLatencyInSamples())),
                   std::memory_order_relaxed);

    ratio = 4;
    fsOs  = fs * ratio;

    // Preamp interstage filters (oversampled rate). Coupling HP = the coupling
    // capacitor between valves (blocks the DC the asymmetric clip introduces and
    // tightens the low end); bright LP = grid-stopper / Miller roll-off.
    for (int c = 0; c < kMaxCh; ++c)
    {
        for (int s = 0; s < kMaxStages; ++s)
        {
            preamp[c][s].couplingHp.setHighpass (31.0, 0.707, fsOs);
            preamp[c][s].brightLp.setLowpass (11000.0, 0.707, fsOs);
        }
        outDcBlock[c].setHighpass (12.0, 0.707, fs);
    }

    // Fixed-size comb buffer: max delay is the largest cab comb (~0.34 ms).
    combLen = static_cast<int> (std::ceil (0.001 * fs)) + 4;
    for (int c = 0; c < kMaxCh; ++c)
        combBuf[c].assign (static_cast<size_t> (combLen), 0.0f);
    combWrite = 0;

    smoothedIn.reset (fs, 0.02);
    smoothedOut.reset (fs, 0.02);

    updateToneCoeffs();
    updateCabCoeffs();
    reset();
}

void AmpModule::reset()
{
    if (oversampler != nullptr)
        oversampler->reset();

    for (int c = 0; c < kMaxCh; ++c)
    {
        for (int s = 0; s < kMaxStages; ++s)
        {
            preamp[c][s].couplingHp.reset();
            preamp[c][s].brightLp.reset();
        }
        bassShelf[c].reset();  midPeak[c].reset();
        trebleShelf[c].reset(); presenceShelf[c].reset();
        cabHp[c].reset(); cabLowRes[c].reset(); cabScoop[c].reset();
        cabCone[c].reset(); cabLp[c].reset(); outDcBlock[c].reset();
        std::fill (combBuf[c].begin(), combBuf[c].end(), 0.0f);
    }
    combWrite = 0;

    const auto& v = kVoicings[juce::jlimit (0, 2, static_cast<int> (channel.get()))];
    smoothedIn.setCurrentAndTargetValue (fromDb (v.driveBaseDb + (gain.get() / 10.0f) * v.driveRangeDb));
    smoothedOut.setCurrentAndTargetValue (
        fromDb (juce::jmap (master.get() / 10.0f, -30.0f, 6.0f) + v.makeupDb));
}

void AmpModule::bindParameter (const juce::String& id, std::atomic<float>* v)
{
    if (v == nullptr) return;

    if      (id == "channel")  channel.value = v;
    else if (id == "gain")     gain.value = v;
    else if (id == "bass")     bass.value = v;
    else if (id == "mid")      mid.value = v;
    else if (id == "treble")   treble.value = v;
    else if (id == "presence") presence.value = v;
    else if (id == "master")   master.value = v;
    else if (id == "cab")      cab.value = v;
}

void AmpModule::updateToneCoeffs() noexcept
{
    // Knobs 0..10, centre 5 = flat. Passive stacks are not flat at noon, but a
    // shelf/peak model with a neutral centre is the intuitive control law; the
    // full interacting network (Yeh & Smith 2006) would add the inherent scoop.
    const float bassDb   = (bass.get()   - 5.0f) / 5.0f * 12.0f;
    const float midDb    = (mid.get()    - 5.0f) / 5.0f * 12.0f;
    const float trebDb   = (treble.get() - 5.0f) / 5.0f * 12.0f;
    const float presDb   = presence.get() / 10.0f * 10.0f;   // 0..+10, boost-only

    for (int c = 0; c < kMaxCh; ++c)
    {
        bassShelf[c].setLowShelf (100.0, 0.707, bassDb, fsOs);
        midPeak[c].setPeak (500.0, 0.7, midDb, fsOs);
        trebleShelf[c].setHighShelf (2500.0, 0.707, trebDb, fsOs);
        presenceShelf[c].setHighShelf (3500.0, 0.707, presDb, fsOs);
    }
}

void AmpModule::updateCabCoeffs() noexcept
{
    const auto& cv = kCabs[juce::jlimit (0, 2, static_cast<int> (cab.get()))];

    for (int c = 0; c < kMaxCh; ++c)
    {
        cabHp[c].setHighpass (cv.hpHz, 0.707, fs);
        cabLowRes[c].setPeak (cv.resHz, cv.resQ, cv.resDb, fs);
        if (cv.scoopDb != 0.0)
            cabScoop[c].setPeak (cv.scoopHz, cv.scoopQ, cv.scoopDb, fs);
        else
            cabScoop[c].setIdentity();
        cabCone[c].setPeak (cv.coneHz, cv.coneQ, cv.coneDb, fs);
        cabLp[c].setLowpass (cv.lpHz, 0.707, fs);
    }
}

void AmpModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext&) noexcept
{
    const auto numSamples  = static_cast<int> (block.getNumSamples());
    const auto numChannels = juce::jmin (static_cast<int> (block.getNumChannels()), kMaxCh);

    const int   chanIdx = juce::jlimit (0, 2, static_cast<int> (channel.get()));
    const auto& voice   = kVoicings[chanIdx];
    const int   stages  = voice.stages;
    const float interS  = voice.interStage;
    const float powerDrive = 1.3f;   // power-amp saturation amount

    // Block-rate coefficient refresh (tone knobs + cab selection).
    updateToneCoeffs();
    updateCabCoeffs();

    const auto& cv = kCabs[juce::jlimit (0, 2, static_cast<int> (cab.get()))];
    const int   combD = juce::jlimit (1, combLen - 1,
                                      static_cast<int> (std::round (cv.combMs * 0.001 * fs)));
    const float combMix = static_cast<float> (cv.combMix);
    const float combNorm = 1.0f / (1.0f + combMix);

    smoothedIn.setTargetValue (fromDb (voice.driveBaseDb + (gain.get() / 10.0f) * voice.driveRangeDb));
    smoothedOut.setTargetValue (
        fromDb (juce::jmap (master.get() / 10.0f, -30.0f, 6.0f) + voice.makeupDb));

    juce::dsp::AudioBlock<float> mainBlock = block.getSubsetChannelBlock (
        0, static_cast<size_t> (numChannels));

    // --- oversampled nonlinear section: preamp -> tone -> power amp ---------
    auto up = oversampler->processSamplesUp (mainBlock);
    const auto upSamples  = static_cast<int> (up.getNumSamples());
    const auto upChannels = juce::jmin (static_cast<int> (up.getNumChannels()), kMaxCh);
    const int  osRatio    = upSamples / juce::jmax (1, numSamples);

    int upIdx = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float inGain = smoothedIn.getNextValue();

        for (int k = 0; k < osRatio && upIdx + k < upSamples; ++k)
            for (int ch = 0; ch < upChannels; ++ch)
            {
                auto* d = up.getChannelPointer (static_cast<size_t> (ch));
                float s = d[upIdx + k] * inGain;

                for (int st = 0; st < stages; ++st)
                {
                    s = tubeStage (s);
                    s = preamp[ch][st].couplingHp.process (s);
                    s = preamp[ch][st].brightLp.process (s);
                    if (st + 1 < stages)
                        s *= interS;
                }

                s = bassShelf[ch].process (s);
                s = midPeak[ch].process (s);
                s = trebleShelf[ch].process (s);
                s = presenceShelf[ch].process (s);

                d[upIdx + k] = std::tanh (powerDrive * s);
            }

        upIdx += osRatio;
    }

    oversampler->processSamplesDown (mainBlock);

    // --- base-rate cabinet + output ----------------------------------------
    float peak[2] = { 0.0f, 0.0f };
    double sumSq[2] = { 0.0, 0.0 };

    for (int i = 0; i < numSamples; ++i)
    {
        const float outGain = smoothedOut.getNextValue();
        const int   readIdx = (combWrite - combD + combLen) % combLen;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (static_cast<size_t> (ch));
            float s = data[i];

            s = cabHp[ch].process (s);
            s = cabLowRes[ch].process (s);
            s = cabScoop[ch].process (s);
            s = cabCone[ch].process (s);
            s = cabLp[ch].process (s);

            const float delayed = combBuf[ch][static_cast<size_t> (readIdx)];
            combBuf[ch][static_cast<size_t> (combWrite)] = s;
            s = (s + combMix * delayed) * combNorm;

            s = outDcBlock[ch].process (s) * outGain;
            data[i] = s;

            peak[ch]  = juce::jmax (peak[ch], std::abs (s));
            sumSq[ch] += static_cast<double> (s) * static_cast<double> (s);
        }

        combWrite = (combWrite + 1) % combLen;
    }

    meterPeakL.store (peak[0], std::memory_order_relaxed);
    meterPeakR.store (numChannels > 1 ? peak[1] : peak[0], std::memory_order_relaxed);
    const auto n = static_cast<double> (juce::jmax (1, numSamples));
    meterRmsL.store (static_cast<float> (std::sqrt (sumSq[0] / n)), std::memory_order_relaxed);
    meterRmsR.store (static_cast<float> (std::sqrt (sumSq[numChannels > 1 ? 1 : 0] / n)),
                     std::memory_order_relaxed);
}

void AmpModule::getMeterFrame (MeterFrame& frame) const noexcept
{
    frame.peakL = meterPeakL.load (std::memory_order_relaxed);
    frame.peakR = meterPeakR.load (std::memory_order_relaxed);
    frame.rmsL  = meterRmsL.load (std::memory_order_relaxed);
    frame.rmsR  = meterRmsR.load (std::memory_order_relaxed);
    frame.gainReductionDb = 0.0f;
}

} // namespace audiorack
