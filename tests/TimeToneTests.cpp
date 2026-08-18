// M6 verification: EQ magnitude response, delay timing/sync, reverb energy
// decay + freeze, saturator harmonic generation and level compensation.

#include <dsp/eq/EqModule.h>
#include <dsp/delay/DelayModule.h>
#include <dsp/reverb/ReverbModule.h>
#include <dsp/saturator/SaturatorModule.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

using namespace audiorack;
using Catch::Approx;

namespace
{

constexpr double kFs = 48000.0;
constexpr int    kBlock = 512;

float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }
float linToDb (float v)  { return 20.0f * std::log10 (std::max (1.0e-9f, v)); }

struct Params
{
    std::map<juce::String, std::unique_ptr<std::atomic<float>>> values;
    void set (const juce::String& id, float v)
    {
        auto it = values.find (id);
        if (it == values.end())
            it = values.emplace (id, std::make_unique<std::atomic<float>> (v)).first;
        it->second->store (v);
    }
    void bindAll (AudioModule& m)
    {
        for (auto& [id, atom] : values)
            m.bindParameter (id, atom.get());
    }
};

juce::AudioBuffer<float> sine (float freq, float ampDb, double seconds, int channels = 2)
{
    const int n = static_cast<int> (seconds * kFs);
    juce::AudioBuffer<float> buf (channels, n);
    const float amp = dbToLin (ampDb);
    for (int i = 0; i < n; ++i)
    {
        const float v = amp * std::sin (2.0f * juce::MathConstants<float>::pi
                                        * freq * static_cast<float> (i) / static_cast<float> (kFs));
        for (int ch = 0; ch < channels; ++ch)
            buf.setSample (ch, i, v);
    }
    return buf;
}

juce::AudioBuffer<float> run (AudioModule& m, const juce::AudioBuffer<float>& input,
                              double bpm = 120.0)
{
    juce::AudioBuffer<float> out;
    out.makeCopyOf (input);
    const int total = out.getNumSamples();

    for (int start = 0; start < total; start += kBlock)
    {
        const int len = std::min (kBlock, total - start);
        float* chans[2] = { out.getWritePointer (0) + start,
                            out.getNumChannels() > 1 ? out.getWritePointer (1) + start : nullptr };
        juce::dsp::AudioBlock<float> block (chans, static_cast<size_t> (out.getNumChannels()),
                                            static_cast<size_t> (len));
        ProcessContext ctx;
        ctx.transport.bpm = bpm;
        m.process (block, ctx);
    }
    return out;
}

/// RMS of channel 0 over [fromSec, toSec).
float rmsBetween (const juce::AudioBuffer<float>& buf, double fromSec, double toSec)
{
    const int a = std::max (0, static_cast<int> (fromSec * kFs));
    const int b = std::min (buf.getNumSamples(), static_cast<int> (toSec * kFs));
    double sum = 0.0;
    for (int i = a; i < b; ++i)
        sum += static_cast<double> (buf.getSample (0, i)) * buf.getSample (0, i);
    return b > a ? static_cast<float> (std::sqrt (sum / (b - a))) : 0.0f;
}

/// Steady-state gain (dB) of a module at a given frequency, using a settled
/// window so filter transients are excluded.
float gainAtDb (AudioModule& m, float freq)
{
    const auto in  = sine (freq, -12.0f, 1.0);
    const auto out = run (m, in);
    return linToDb (rmsBetween (out, 0.6, 0.95)) - linToDb (rmsBetween (in, 0.6, 0.95));
}

} // namespace

// ================================== EQ ==================================

TEST_CASE ("EQ bell band boosts its centre and leaves distant frequencies flat", "[eq]")
{
    Params p;
    // Disable all bands, then set band 3 (index 3) to a +12 dB bell at 1 kHz.
    for (int i = 0; i < EqModule::kBands; ++i)
        p.set ("b" + juce::String (i) + "on", 0.0f);

    p.set ("b3on", 1.0f);
    p.set ("b3type", 0.0f);          // Bell
    p.set ("b3freq", 1000.0f);
    p.set ("b3gain", 12.0f);
    p.set ("b3q", 2.0f);

    EqModule eq;
    p.bindAll (eq);
    eq.prepare (kFs, kBlock, 2);

    CHECK (gainAtDb (eq, 1000.0f) == Approx (12.0f).margin (1.5));

    EqModule eq2; p.bindAll (eq2); eq2.prepare (kFs, kBlock, 2);
    CHECK (gainAtDb (eq2, 60.0f) == Approx (0.0f).margin (1.5));

    EqModule eq3; p.bindAll (eq3); eq3.prepare (kFs, kBlock, 2);
    CHECK (gainAtDb (eq3, 12000.0f) == Approx (0.0f).margin (1.5));
}

TEST_CASE ("EQ highpass attenuates below cutoff, passes above", "[eq]")
{
    Params p;
    for (int i = 0; i < EqModule::kBands; ++i)
        p.set ("b" + juce::String (i) + "on", 0.0f);

    p.set ("b0on", 1.0f);
    p.set ("b0type", 3.0f);          // High Pass
    p.set ("b0freq", 500.0f);
    p.set ("b0q", 0.707f);

    EqModule eq; p.bindAll (eq); eq.prepare (kFs, kBlock, 2);
    CHECK (gainAtDb (eq, 100.0f) < -10.0f);

    EqModule eq2; p.bindAll (eq2); eq2.prepare (kFs, kBlock, 2);
    CHECK (gainAtDb (eq2, 5000.0f) == Approx (0.0f).margin (1.0));
}

TEST_CASE ("EQ solo isolates the soloed band's bands only", "[eq]")
{
    Params p;
    for (int i = 0; i < EqModule::kBands; ++i)
    {
        p.set ("b" + juce::String (i) + "on", 1.0f);
        p.set ("b" + juce::String (i) + "gain", 0.0f);
    }
    // Band 1 is a boosting bell, but band 4 is soloed and flat -> at band 1's
    // frequency we should now measure ~0 dB, because band 1 is excluded.
    p.set ("b1type", 0.0f); p.set ("b1freq", 200.0f); p.set ("b1gain", 12.0f); p.set ("b1q", 3.0f);
    p.set ("b4solo", 1.0f); p.set ("b4type", 0.0f); p.set ("b4freq", 8000.0f); p.set ("b4gain", 0.0f);

    EqModule eq; p.bindAll (eq); eq.prepare (kFs, kBlock, 2);
    CHECK (gainAtDb (eq, 200.0f) == Approx (0.0f).margin (1.5));
}

// ================================ DELAY ================================

TEST_CASE ("Delay reproduces the input at the set time", "[delay]")
{
    Params p;
    p.set ("time", 100.0f); p.set ("feedback", 0.0f); p.set ("mix", 1.0f);
    p.set ("sync", 0.0f); p.set ("mode", 0.0f); p.set ("offset", 0.0f);

    DelayModule d; p.bindAll (d); d.prepare (kFs, kBlock, 2);

    juce::AudioBuffer<float> input (2, static_cast<int> (0.5 * kFs));
    input.clear();
    input.setSample (0, 100, 1.0f);
    input.setSample (1, 100, 1.0f);

    const auto out = run (d, input);

    // 100 ms = 4800 samples; wet-only, so the echo is at 100 + 4800.
    int peakIndex = 0; float peak = 0.0f;
    for (int i = 200; i < out.getNumSamples(); ++i)
        if (std::abs (out.getSample (0, i)) > peak) { peak = std::abs (out.getSample (0, i)); peakIndex = i; }

    CHECK (peakIndex == Approx (100 + 4800).margin (2));
    CHECK (peak > 0.7f);
}

TEST_CASE ("Delay host-sync locks a quarter note to the tempo", "[delay][sync]")
{
    Params p;
    p.set ("sync", 1.0f); p.set ("division", 4.0f);   // 1/4
    p.set ("feedback", 0.0f); p.set ("mix", 1.0f); p.set ("mode", 0.0f);

    DelayModule d; p.bindAll (d); d.prepare (kFs, kBlock, 2);

    juce::AudioBuffer<float> input (2, static_cast<int> (1.0 * kFs));
    input.clear();
    input.setSample (0, 50, 1.0f);
    input.setSample (1, 50, 1.0f);

    const auto out = run (d, input, 120.0);   // 120 bpm -> 1/4 = 0.5 s = 24000 samples

    int peakIndex = 0; float peak = 0.0f;
    for (int i = 1000; i < out.getNumSamples(); ++i)
        if (std::abs (out.getSample (0, i)) > peak) { peak = std::abs (out.getSample (0, i)); peakIndex = i; }

    CHECK (peakIndex == Approx (50 + 24000).margin (3));
}

TEST_CASE ("Delay feedback produces decaying repeats", "[delay]")
{
    Params p;
    p.set ("time", 50.0f); p.set ("feedback", 0.5f); p.set ("mix", 1.0f);
    p.set ("sync", 0.0f); p.set ("mode", 0.0f);

    DelayModule d; p.bindAll (d); d.prepare (kFs, kBlock, 2);

    juce::AudioBuffer<float> input (2, static_cast<int> (0.5 * kFs));
    input.clear();
    input.setSample (0, 10, 1.0f);
    input.setSample (1, 10, 1.0f);

    const auto out = run (d, input);
    const int step = static_cast<int> (0.05 * kFs);

    const float echo1 = std::abs (out.getSample (0, 10 + step));
    const float echo2 = std::abs (out.getSample (0, 10 + 2 * step));

    CHECK (echo1 > 0.3f);
    CHECK (echo2 > 0.1f);
    CHECK (echo2 < echo1);       // decaying
}

// ================================ REVERB ================================

TEST_CASE ("Reverb produces a decaying tail after the input stops", "[reverb]")
{
    Params p;
    p.set ("size", 0.6f); p.set ("decay", 0.6f); p.set ("damping", 0.5f);
    p.set ("predelay", 0.0f); p.set ("mix", 1.0f); p.set ("width", 1.0f); p.set ("freeze", 0.0f);

    ReverbModule r; p.bindAll (r); r.prepare (kFs, kBlock, 2);

    // 50 ms burst, then 2 s silence.
    auto input = sine (500.0f, -6.0f, 2.0);
    for (int ch = 0; ch < 2; ++ch)
        input.clear (ch, static_cast<int> (0.05 * kFs), input.getNumSamples() - static_cast<int> (0.05 * kFs));

    const auto out = run (r, input);

    const float early = rmsBetween (out, 0.1, 0.3);
    const float late  = rmsBetween (out, 1.0, 1.2);

    CHECK (early > 1.0e-4f);      // there is a tail
    CHECK (late < early);        // it decays
    CHECK (late > 0.0f);
}

TEST_CASE ("Reverb freeze sustains the tail indefinitely", "[reverb]")
{
    Params p;
    p.set ("size", 0.6f); p.set ("decay", 0.6f); p.set ("damping", 0.3f);
    p.set ("predelay", 0.0f); p.set ("mix", 1.0f); p.set ("width", 1.0f); p.set ("freeze", 1.0f);

    ReverbModule r; p.bindAll (r); r.prepare (kFs, kBlock, 2);

    auto input = sine (500.0f, -6.0f, 3.0);
    for (int ch = 0; ch < 2; ++ch)
        input.clear (ch, static_cast<int> (0.1 * kFs), input.getNumSamples() - static_cast<int> (0.1 * kFs));

    const auto out = run (r, input);

    const float mid  = rmsBetween (out, 1.0, 1.2);
    const float late = rmsBetween (out, 2.5, 2.7);

    // Frozen: energy should be sustained, not decaying to nothing.
    CHECK (mid > 1.0e-3f);
    CHECK (late > mid * 0.5f);
    CHECK (std::isfinite (late));
}

// =============================== SATURATOR ===============================

TEST_CASE ("Saturator generates harmonics from a pure sine", "[sat]")
{
    Params p;
    p.set ("drive", 18.0f); p.set ("type", 1.0f);   // Tape
    p.set ("bias", 0.0f); p.set ("out", 0.0f); p.set ("mix", 1.0f); p.set ("autogain", 1.0f);

    SaturatorModule s; p.bindAll (s); s.prepare (kFs, kBlock, 2);

    const float freq = 1000.0f;
    const auto in  = sine (freq, -6.0f, 0.5);
    const auto out = run (s, in);

    // Goertzel magnitude at the fundamental and the 3rd harmonic over a
    // settled window.
    auto goertzel = [&] (const juce::AudioBuffer<float>& buf, float f)
    {
        const int a = static_cast<int> (0.2 * kFs), b = static_cast<int> (0.45 * kFs);
        const double w = 2.0 * juce::MathConstants<double>::pi * f / kFs;
        const double cw = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = a; i < b; ++i) { const double s0 = buf.getSample (0, i) + cw * s1 - s2; s2 = s1; s1 = s0; }
        return std::sqrt (s1 * s1 + s2 * s2 - cw * s1 * s2);
    };

    const double fund = goertzel (out, freq);
    const double h3   = goertzel (out, 3.0f * freq);

    CHECK (fund > 0.0);
    CHECK (h3 / fund > 0.01);        // measurable odd-harmonic content
}

TEST_CASE ("Saturator auto-gain keeps output level roughly stable across drive", "[sat]")
{
    auto outputRms = [] (float driveDb)
    {
        Params p;
        p.set ("drive", driveDb); p.set ("type", 1.0f);
        p.set ("bias", 0.0f); p.set ("out", 0.0f); p.set ("mix", 1.0f); p.set ("autogain", 1.0f);
        SaturatorModule s; p.bindAll (s); s.prepare (kFs, kBlock, 2);
        const auto out = run (s, sine (500.0f, -18.0f, 0.4));
        return rmsBetween (out, 0.2, 0.38);
    };

    const float low  = outputRms (3.0f);
    const float high = outputRms (24.0f);

    // Without compensation, 21 dB more drive would be ~21 dB louder; auto-gain
    // must keep them within a few dB.
    CHECK (std::abs (linToDb (high) - linToDb (low)) < 8.0f);
}

TEST_CASE ("Saturator reports oversampler latency", "[sat][latency]")
{
    Params p;
    p.set ("drive", 6.0f); p.set ("type", 0.0f);
    SaturatorModule s; p.bindAll (s); s.prepare (kFs, kBlock, 2);
    CHECK (s.latencySamples() > 0);
}
