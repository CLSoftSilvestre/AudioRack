// M5 dynamics verification: static curve, timing, null and behavioural tests
// for CompressorModule, LimiterModule and GateModule. Every claim the module
// docs make is measured here.

#include <dsp/compressor/CompressorModule.h>
#include <dsp/limiter/LimiterModule.h>
#include <dsp/gate/GateModule.h>

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
float linToDb (float v)  { return 20.0f * std::log10 (std::max (1.0e-9f, v)); }

/// Owns parameter atomics and binds them by suffix.
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

/// Processes `input` in blocks; returns the processed buffer.
/// `sidechain` (optional) must be at least as long as input.
juce::AudioBuffer<float> run (AudioModule& m, const juce::AudioBuffer<float>& input,
                              const juce::AudioBuffer<float>* sidechain = nullptr)
{
    juce::AudioBuffer<float> out;
    out.makeCopyOf (input);

    const int total = out.getNumSamples();

    for (int start = 0; start < total; start += kBlock)
    {
        const int len = std::min (kBlock, total - start);

        float* chans[2] = { out.getWritePointer (0) + start,
                            out.getNumChannels() > 1 ? out.getWritePointer (1) + start : nullptr };
        juce::dsp::AudioBlock<float> block (chans,
                                            static_cast<size_t> (out.getNumChannels()),
                                            static_cast<size_t> (len));

        ProcessContext ctx;
        const float* scChans[2] = { nullptr, nullptr };

        if (sidechain != nullptr)
        {
            for (int ch = 0; ch < sidechain->getNumChannels() && ch < 2; ++ch)
                scChans[ch] = sidechain->getReadPointer (ch) + start;

            ctx.sidechain            = scChans;
            ctx.numSidechainChannels = std::min (2, sidechain->getNumChannels());
            ctx.numSidechainSamples  = len;
        }

        m.process (block, ctx);
    }

    return out;
}

/// Peak amplitude over [fromSec, toSec).
float peakBetween (const juce::AudioBuffer<float>& buf, double fromSec, double toSec)
{
    const int a = static_cast<int> (fromSec * kFs);
    const int b = std::min (buf.getNumSamples(), static_cast<int> (toSec * kFs));
    float peak = 0.0f;
    for (int i = a; i < b; ++i)
        peak = std::max (peak, std::abs (buf.getSample (0, i)));
    return peak;
}

/// Per-crest gain reduction trace: samples out/in wherever |in| is near the
/// crest of the sine, giving a clean GR-vs-time curve.
std::vector<std::pair<double, float>> grTrace (const juce::AudioBuffer<float>& in,
                                               const juce::AudioBuffer<float>& out,
                                               float crestThreshold)
{
    std::vector<std::pair<double, float>> trace;
    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        const float x = std::abs (in.getSample (0, i));
        if (x > crestThreshold)
        {
            const float g = std::abs (out.getSample (0, i)) / x;
            trace.emplace_back (static_cast<double> (i) / kFs, -linToDb (g));
        }
    }
    return trace;
}

} // namespace

// ============================== COMPRESSOR ==============================

TEST_CASE ("Compressor static curve: unity below threshold, 1/R slope above", "[comp]")
{
    Params p;
    p.set ("threshold", -20.0f); p.set ("ratio", 4.0f); p.set ("knee", 0.0f);
    p.set ("attack", 0.5f);      p.set ("release", 500.0f);

    auto measure = [&] (float inDb)
    {
        CompressorModule c;
        p.bindAll (c);
        c.prepare (kFs, kBlock, 2);
        const auto out = run (c, sine (1000.0f, inDb, 1.5));
        return linToDb (peakBetween (out, 1.2, 1.5));
    };

    // Below threshold: unity.
    CHECK (measure (-30.0f) == Approx (-30.0f).margin (0.5));

    // Above: out = T + (in - T)/R.
    CHECK (measure (-10.0f) == Approx (-20.0f + 10.0f / 4.0f).margin (1.0));
    CHECK (measure (-4.0f)  == Approx (-20.0f + 16.0f / 4.0f).margin (1.0));

    // Slope between the two points ~ 1/R.
    const float slope = (measure (-4.0f) - measure (-10.0f)) / 6.0f;
    CHECK (slope == Approx (0.25f).margin (0.06));
}

TEST_CASE ("Compressor soft knee: half-ratio GR exactly at threshold", "[comp]")
{
    Params p;
    p.set ("threshold", -20.0f); p.set ("ratio", 4.0f); p.set ("knee", 12.0f);
    p.set ("attack", 0.5f);      p.set ("release", 500.0f);

    CompressorModule c;
    p.bindAll (c);
    c.prepare (kFs, kBlock, 2);

    // At L = T the knee formula gives (1/R - 1) * (W/2)^2 / (2W) = -1.125 dB.
    const auto out = run (c, sine (1000.0f, -20.0f, 1.5));
    CHECK (linToDb (peakBetween (out, 1.2, 1.5)) == Approx (-21.125f).margin (0.6));
}

TEST_CASE ("Compressor attack and release timing", "[comp][timing]")
{
    Params p;
    p.set ("threshold", -30.0f); p.set ("ratio", 20.0f); p.set ("knee", 0.0f);
    p.set ("attack", 10.0f);     p.set ("release", 200.0f);

    CompressorModule c;
    p.bindAll (c);
    c.prepare (kFs, kBlock, 2);

    // 0.5 s at -6 dB (attack phase), then 1.2 s at -35 dB (release phase).
    auto input = sine (1000.0f, -6.0f, 1.7);
    {
        const auto quiet = sine (1000.0f, -35.0f, 1.2);
        for (int ch = 0; ch < 2; ++ch)
            input.copyFrom (ch, static_cast<int> (0.5 * kFs), quiet, ch, 0, quiet.getNumSamples());
    }

    const auto out = run (c, input);
    const auto trace = grTrace (input, out, dbToLin (-8.0f));   // crests of the loud part

    const float grFinal = 24.0f * (1.0f - 1.0f / 20.0f);        // 22.8 dB

    // Attack: time to reach 63% of final GR should be ~ the 10 ms time constant.
    double tAttack = -1.0;
    for (const auto& [t, gr] : trace)
        if (t < 0.5 && gr >= 0.63f * grFinal) { tAttack = t; break; }

    REQUIRE (tAttack > 0.0);
    CHECK (tAttack > 0.004);
    CHECK (tAttack < 0.030);

    // Release: GR should decay to 37% of its settled value ~ one 200 ms tau
    // after the level drop at t = 0.5 s.
    const auto quietTrace = grTrace (input, out, dbToLin (-37.0f));
    double tRelease = -1.0;
    for (const auto& [t, gr] : quietTrace)
        if (t > 0.52 && gr <= 0.37f * grFinal) { tRelease = t - 0.5; break; }

    REQUIRE (tRelease > 0.0);
    CHECK (tRelease > 0.08);
    CHECK (tRelease < 0.50);
}

TEST_CASE ("Compressor external sidechain drives gain reduction", "[comp][sidechain]")
{
    Params p;
    p.set ("threshold", -20.0f); p.set ("ratio", 8.0f); p.set ("knee", 0.0f);
    p.set ("attack", 1.0f);      p.set ("release", 500.0f);
    p.set ("scsource", 1.0f);    // External

    CompressorModule c;
    p.bindAll (c);
    c.prepare (kFs, kBlock, 2);

    // Main programme is below threshold; the external sidechain is loud.
    const auto main = sine (1000.0f, -30.0f, 1.0);
    const auto sc   = sine (200.0f, -5.0f, 1.0);
    const auto out  = run (c, main, &sc);

    // GR expected ~ ((-5) - (-20)) * (1 - 1/8) = 13.1 dB on the main signal.
    const float outDb = linToDb (peakBetween (out, 0.7, 1.0));
    CHECK (outDb < -30.0f - 8.0f);
    CHECK (outDb > -30.0f - 18.0f);
}

TEST_CASE ("Compressor sidechain HPF removes bass from the detector", "[comp][sidechain]")
{
    auto grFor = [] (float hpf)
    {
        Params p;
        p.set ("threshold", -30.0f); p.set ("ratio", 8.0f); p.set ("knee", 0.0f);
        p.set ("attack", 1.0f);      p.set ("release", 500.0f);
        p.set ("schpf", hpf);

        CompressorModule c;
        p.bindAll (c);
        c.prepare (kFs, kBlock, 2);

        const auto out = run (c, sine (60.0f, -6.0f, 1.0));
        return -6.0f - linToDb (peakBetween (out, 0.7, 1.0));   // GR in dB
    };

    const float grOpen     = grFor (20.0f);
    const float grFiltered = grFor (500.0f);

    CHECK (grOpen > 15.0f);                    // 60 Hz at -6 dB over -30 threshold
    CHECK (grFiltered < grOpen - 10.0f);       // HPF at 500 Hz kills most of it
}

TEST_CASE ("Compressor auto-makeup compensates half the full-scale GR", "[comp]")
{
    Params p;
    p.set ("threshold", -20.0f); p.set ("ratio", 4.0f); p.set ("knee", 0.0f);
    p.set ("attack", 1.0f);      p.set ("release", 500.0f);
    p.set ("automakeup", 1.0f);

    CompressorModule c;
    p.bindAll (c);
    c.prepare (kFs, kBlock, 2);

    // Auto makeup = -G(0)/2 = 7.5 dB. Signal below threshold passes at
    // unity + makeup.
    const auto out = run (c, sine (1000.0f, -40.0f, 1.0));
    CHECK (linToDb (peakBetween (out, 0.7, 1.0)) == Approx (-32.5f).margin (1.0));
}

TEST_CASE ("Compressor feed-back topology compresses stably", "[comp]")
{
    Params p;
    p.set ("threshold", -30.0f); p.set ("ratio", 4.0f); p.set ("knee", 0.0f);
    p.set ("attack", 1.0f);      p.set ("release", 300.0f);
    p.set ("topology", 1.0f);    // Feed-Back

    CompressorModule c;
    p.bindAll (c);
    c.prepare (kFs, kBlock, 2);

    const auto out = run (c, sine (1000.0f, -6.0f, 1.5));
    const float outDb = linToDb (peakBetween (out, 1.2, 1.5));

    // FB reaches less GR than FF for the same settings, but must compress
    // and must be stable (no oscillation/blow-up).
    CHECK (outDb < -12.0f);
    CHECK (outDb > -30.0f);
    CHECK (std::isfinite (outDb));
}

// ================================ LIMITER ================================

TEST_CASE ("Limiter reports its true latency", "[lim][latency]")
{
    Params p;
    p.set ("ceiling", -0.3f); p.set ("release", 100.0f); p.set ("lookahead", 2.0f);

    LimiterModule l;
    p.bindAll (l);
    l.prepare (kFs, kBlock, 2);

    juce::AudioBuffer<float> impulse (2, 8192);
    impulse.clear();
    impulse.setSample (0, 1000, 0.1f);
    impulse.setSample (1, 1000, 0.1f);

    const auto out = run (l, impulse);

    int peakIndex = 0;
    float peak = 0.0f;
    for (int i = 0; i < out.getNumSamples(); ++i)
        if (std::abs (out.getSample (0, i)) > peak)
        {
            peak = std::abs (out.getSample (0, i));
            peakIndex = i;
        }

    CHECK (peakIndex == 1000 + l.latencySamples());
    CHECK (l.latencySamples() > static_cast<int> (0.002 * kFs) - 2);
}

TEST_CASE ("Limiter latency follows the lookahead parameter", "[lim][latency]")
{
    auto latencyFor = [] (float ms)
    {
        Params p;
        p.set ("ceiling", -0.3f); p.set ("release", 100.0f); p.set ("lookahead", ms);
        LimiterModule l;
        p.bindAll (l);
        l.prepare (kFs, kBlock, 2);
        return l.latencySamples();
    };

    const int diff = latencyFor (5.0f) - latencyFor (1.0f);
    CHECK (diff == Approx (0.004 * kFs).margin (2.0));
}

TEST_CASE ("Limiter null test: signal under the ceiling passes bit-transparently", "[lim][null]")
{
    Params p;
    p.set ("ceiling", -0.3f); p.set ("release", 100.0f); p.set ("lookahead", 2.0f);

    LimiterModule l;
    p.bindAll (l);
    l.prepare (kFs, kBlock, 2);

    const auto input = sine (997.0f, -12.0f, 1.0);
    const auto out   = run (l, input);
    const int  D     = l.latencySamples();

    float maxDiff = 0.0f;
    for (int i = 0; i + D < input.getNumSamples(); ++i)
        maxDiff = std::max (maxDiff,
                            std::abs (out.getSample (0, i + D) - input.getSample (0, i)));

    CHECK (maxDiff < 1.0e-4f);
}

TEST_CASE ("Limiter never overshoots its ceiling", "[lim]")
{
    Params p;
    p.set ("ceiling", -1.0f); p.set ("release", 50.0f); p.set ("lookahead", 2.0f);

    LimiterModule l;
    p.bindAll (l);
    l.prepare (kFs, kBlock, 2);

    // Hostile programme: hot sine + spikes + square bursts, ~+6 dB over.
    auto input = sine (997.0f, 0.0f, 1.0);
    for (int i = 4000; i < input.getNumSamples(); i += 6000)
    {
        input.setSample (0, i, 1.9f);
        input.setSample (1, i, -1.9f);
    }
    for (int i = 24000; i < 26000; ++i)
    {
        input.setSample (0, i, (i / 50) % 2 == 0 ? 1.5f : -1.5f);
        input.setSample (1, i, (i / 50) % 2 == 0 ? 1.5f : -1.5f);
    }

    const auto out = run (l, input);
    const float ceilLin = dbToLin (-1.0f);

    float maxAbs = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            maxAbs = std::max (maxAbs, std::abs (out.getSample (ch, i)));

    CHECK (maxAbs <= ceilLin * 1.0001f);
    CHECK (maxAbs > ceilLin * 0.7f);       // it is actually limiting, not muting
}

// ================================== GATE ==================================

TEST_CASE ("Gate attenuates below-threshold signal by the range", "[gate]")
{
    Params p;
    p.set ("threshold", -40.0f); p.set ("range", 60.0f);
    p.set ("attack", 0.5f); p.set ("hold", 0.0f); p.set ("release", 50.0f);

    GateModule g;
    p.bindAll (g);
    g.prepare (kFs, kBlock, 2);

    const auto out = run (g, sine (1000.0f, -60.0f, 1.0));
    const float attenuation = -60.0f - linToDb (peakBetween (out, 0.7, 1.0));
    CHECK (attenuation > 50.0f);
}

TEST_CASE ("Gate opens fast on programme and holds then releases", "[gate][timing]")
{
    Params p;
    p.set ("threshold", -30.0f); p.set ("range", 80.0f); p.set ("hysteresis", 3.0f);
    p.set ("attack", 0.5f); p.set ("hold", 80.0f); p.set ("release", 100.0f);

    GateModule g;
    p.bindAll (g);
    g.prepare (kFs, kBlock, 2);

    // 0.4 s tone at -6, then silence.
    auto input = sine (1000.0f, -6.0f, 1.2);
    for (int ch = 0; ch < 2; ++ch)
        input.clear (ch, static_cast<int> (0.4 * kFs), input.getNumSamples() - static_cast<int> (0.4 * kFs));

    const auto out = run (g, input);

    // Open: within 10 ms the tone passes at (nearly) full level.
    CHECK (peakBetween (out, 0.010, 0.020) > dbToLin (-6.0f) * 0.8f);

    // Hold: the gate stays open right after the tone stops... nothing to
    // measure on silence directly, so check the release tail timing instead:
    // inject a tiny probe tone at -50 dB (below threshold) after the stop.
    auto probe = sine (1000.0f, -50.0f, 1.2);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < static_cast<int> (0.4 * kFs); ++i)
            probe.setSample (ch, i, input.getSample (ch, i));

    GateModule g2;
    p.bindAll (g2);
    g2.prepare (kFs, kBlock, 2);
    const auto out2 = run (g2, probe);

    // During hold (~80 ms) the probe passes nearly unattenuated...
    CHECK (peakBetween (out2, 0.42, 0.46) > dbToLin (-50.0f) * 0.7f);
    // ...but two release constants after hold expires it is well attenuated.
    CHECK (peakBetween (out2, 0.9, 1.1) < dbToLin (-50.0f) * 0.1f);
}

TEST_CASE ("Gate hysteresis prevents chatter between thresholds", "[gate]")
{
    Params p;
    p.set ("threshold", -20.0f); p.set ("hysteresis", 12.0f); p.set ("range", 80.0f);
    p.set ("attack", 0.5f); p.set ("hold", 0.0f); p.set ("release", 50.0f);

    GateModule g;
    p.bindAll (g);
    g.prepare (kFs, kBlock, 2);

    // Open with a -6 dB burst, then sit at -26 dB: below the open threshold
    // but above close (-32 dB) -> the gate must stay open.
    auto input = sine (1000.0f, -6.0f, 1.0);
    {
        const auto mid = sine (1000.0f, -26.0f, 0.8);
        for (int ch = 0; ch < 2; ++ch)
            input.copyFrom (ch, static_cast<int> (0.2 * kFs), mid, ch, 0,
                            input.getNumSamples() - static_cast<int> (0.2 * kFs));
    }

    const auto out = run (g, input);
    CHECK (peakBetween (out, 0.8, 1.0) > dbToLin (-26.0f) * 0.85f);
}

TEST_CASE ("Gate lookahead preserves transients that a zero-lookahead gate clips", "[gate][latency]")
{
    auto transientPeak = [] (float lookMs)
    {
        Params p;
        p.set ("threshold", -30.0f); p.set ("range", 80.0f);
        p.set ("attack", 5.0f); p.set ("hold", 50.0f); p.set ("release", 100.0f);
        p.set ("lookahead", lookMs);

        GateModule g;
        p.bindAll (g);
        g.prepare (kFs, kBlock, 2);

        // Single-cycle 1 kHz click from silence.
        juce::AudioBuffer<float> input (2, 24000);
        input.clear();
        for (int i = 0; i < 48; ++i)
        {
            const float v = 0.8f * std::sin (2.0f * juce::MathConstants<float>::pi
                                             * static_cast<float> (i) / 48.0f);
            input.setSample (0, 12000 + i, v);
            input.setSample (1, 12000 + i, v);
        }

        const auto out = run (g, input);

        float peak = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            peak = std::max (peak, std::abs (out.getSample (0, i)));
        return peak;
    };

    const float without = transientPeak (0.0f);
    const float with    = transientPeak (5.0f);

    CHECK (with > without * 1.5f);
    CHECK (with > 0.5f);           // most of the 0.8 transient survives
}
