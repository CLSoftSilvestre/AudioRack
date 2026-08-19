// Guitar amp verification: cascaded preamp generates harmonics (more with
// hotter channels), tone controls move the spectral balance, and each cabinet
// band-limits the top end (a bigger box = darker). Also: bounded, finite output
// and honest oversampler latency.

#include <dsp/amp/AmpModule.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <atomic>
#include <cmath>
#include <map>
#include <memory>

using namespace audiorack;
using Catch::Approx;

namespace
{

constexpr double kFs = 48000.0;
constexpr int    kBlock = 512;

float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }

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

// A sensible default patch; individual tests override what they probe.
Params defaultPatch()
{
    Params p;
    p.set ("channel", 0.0f); p.set ("gain", 5.0f);
    p.set ("bass", 5.0f); p.set ("mid", 5.0f); p.set ("treble", 5.0f);
    p.set ("presence", 5.0f); p.set ("master", 5.0f); p.set ("cab", 1.0f);
    return p;
}

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

juce::AudioBuffer<float> run (AudioModule& m, const juce::AudioBuffer<float>& input)
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
        m.process (block, {});
    }
    return out;
}

// Goertzel magnitude at f over a settled window of channel 0.
double toneMag (const juce::AudioBuffer<float>& buf, double f)
{
    const int a = static_cast<int> (0.3 * kFs), b = static_cast<int> (0.9 * kFs);
    const double w = 2.0 * juce::MathConstants<double>::pi * f / kFs;
    const double cw = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = a; i < b; ++i) { const double s0 = buf.getSample (0, i) + cw * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt (s1 * s1 + s2 * s2 - cw * s1 * s2);
}

// Broadband RMS of channel 0 over a settled window.
double bandRms (const juce::AudioBuffer<float>& buf, double fromSec, double toSec)
{
    const int a = std::max (0, static_cast<int> (fromSec * kFs));
    const int b = std::min (buf.getNumSamples(), static_cast<int> (toSec * kFs));
    double sum = 0.0;
    for (int i = a; i < b; ++i) sum += static_cast<double> (buf.getSample (0, i)) * buf.getSample (0, i);
    return b > a ? std::sqrt (sum / (b - a)) : 0.0;
}

} // namespace

TEST_CASE ("Amp preamp generates harmonics, and hotter channels add more", "[amp]")
{
    // THD-style ratio: harmonics 2..6 over the fundamental, driven with the
    // same (fairly low) input so the channels are distinguished by their gain
    // staging rather than all being slammed into full clipping. Clean should be
    // the most linear, Lead the least.
    auto harmonicRatio = [] (float channelIdx)
    {
        auto p = defaultPatch();
        p.set ("channel", channelIdx);
        p.set ("gain", 5.0f);
        AmpModule a; p.bindAll (a); a.prepare (kFs, kBlock, 2);

        const float f = 220.0f;
        const auto out = run (a, sine (f, -24.0f, 1.0));
        const double fund = std::max (1.0e-9, toneMag (out, f));
        double harmonics = 0.0;
        for (int k = 2; k <= 6; ++k)
            harmonics += toneMag (out, k * f);
        return harmonics / fund;
    };

    const double clean  = harmonicRatio (0.0f);
    const double crunch = harmonicRatio (1.0f);
    const double lead   = harmonicRatio (2.0f);

    CHECK (clean > 0.0);
    CHECK (crunch > clean);
    CHECK (lead   > crunch);
}

TEST_CASE ("Amp tone stack moves the spectral balance", "[amp][tone]")
{
    // Treble control: compare a high-frequency probe with treble at 8 vs 2.
    auto trebleEnergy = [] (float treble)
    {
        auto p = defaultPatch();
        p.set ("channel", 0.0f); p.set ("gain", 3.0f);   // near-clean, so the EQ dominates
        p.set ("treble", treble);
        AmpModule a; p.bindAll (a); a.prepare (kFs, kBlock, 2);
        return toneMag (run (a, sine (4000.0f, -18.0f, 1.0)), 4000.0);
    };
    CHECK (trebleEnergy (8.0f) > trebleEnergy (2.0f) * 1.5);

    auto bassEnergy = [] (float bass)
    {
        auto p = defaultPatch();
        p.set ("channel", 0.0f); p.set ("gain", 3.0f);
        p.set ("bass", bass);
        AmpModule a; p.bindAll (a); a.prepare (kFs, kBlock, 2);
        return toneMag (run (a, sine (80.0f, -18.0f, 1.0)), 80.0);
    };
    CHECK (bassEnergy (8.0f) > bassEnergy (2.0f) * 1.5);
}

TEST_CASE ("Cabinets band-limit the top end; a bigger box is darker", "[amp][cab]")
{
    // Same signal through each cab: high-frequency content must fall as the
    // cabinet grows (1x12 brightest, 4x12 darkest).
    // Sum harmonic energy across a band (harmonics 9..15 => ~3.0-5.0 kHz) that
    // straddles all three cab roll-off corners (4.2 / 4.8 / 5.2 kHz), so a
    // darker cabinet shows unambiguously less energy there.
    auto hfEnergy = [] (float cab)
    {
        auto p = defaultPatch();
        p.set ("channel", 2.0f); p.set ("gain", 8.0f);   // broadband distortion source
        p.set ("cab", cab);
        AmpModule a; p.bindAll (a); a.prepare (kFs, kBlock, 2);
        const auto out = run (a, sine (330.0f, -12.0f, 1.0));
        double e = 0.0;
        for (int k = 9; k <= 15; ++k)
            e += toneMag (out, k * 330.0);
        return e;
    };

    const double h1x12 = hfEnergy (0.0f);
    const double h2x12 = hfEnergy (1.0f);
    const double h4x12 = hfEnergy (2.0f);

    CHECK (h1x12 > h2x12);
    CHECK (h2x12 > h4x12);
}

TEST_CASE ("Amp output stays finite and bounded at extreme settings", "[amp]")
{
    auto p = defaultPatch();
    p.set ("channel", 2.0f); p.set ("gain", 10.0f); p.set ("master", 10.0f);
    p.set ("bass", 10.0f); p.set ("treble", 10.0f); p.set ("presence", 10.0f);
    AmpModule a; p.bindAll (a); a.prepare (kFs, kBlock, 2);

    const auto out = run (a, sine (110.0f, 0.0f, 0.6));

    float maxAbs = 0.0f;
    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        const float v = out.getSample (0, i);
        REQUIRE (std::isfinite (v));
        maxAbs = std::max (maxAbs, std::abs (v));
    }
    CHECK (maxAbs < 8.0f);            // no runaway; a hot but sane ceiling
    CHECK (bandRms (out, 0.3, 0.55) > 0.0);
}

TEST_CASE ("Amp reports oversampler latency", "[amp][latency]")
{
    auto p = defaultPatch();
    AmpModule a; p.bindAll (a); a.prepare (kFs, kBlock, 2);
    CHECK (a.latencySamples() > 0);
}
