// Verification for the shared real-time analyser (src/dsp/common/SpectrumAnalyser.h)
// and the EQ-6 RTA tap.
//
// The analyser's contract is a calibrated one: a full-scale sine must read
// 0 dBFS in the band that contains it, so the RTA can be read as an absolute
// level display rather than a relative shape. These tests pin that calibration,
// the log band mapping, the release ballistics and — the point of the whole
// exercise — that EQ-6 taps its own output, not its input.

#include <dsp/common/SpectrumAnalyser.h>
#include <dsp/eq/EqModule.h>

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

constexpr double kFs    = 48000.0;
constexpr int    kBlock = 512;

/// Index of the band whose range contains `hz` — the same geometric mapping
/// the analyser and the UI use.
int bandFor (float hz)
{
    const auto ratio = std::log (hz / kSpectrumMinHz)
                       / std::log (kSpectrumMaxHz / kSpectrumMinHz);
    return static_cast<int> (ratio * kSpectrumBands);
}

/// Pushes `seconds` of a sine through the analyser in realistic block sizes.
void pushSine (dsp::SpectrumAnalyser& a, float freq, float amp, double seconds)
{
    juce::AudioBuffer<float> buf (2, kBlock);
    const int totalBlocks = static_cast<int> (seconds * kFs) / kBlock;
    int n = 0;

    for (int b = 0; b < totalBlocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i, ++n)
        {
            const float v = amp * std::sin (2.0f * juce::MathConstants<float>::pi
                                            * freq * static_cast<float> (n) / static_cast<float> (kFs));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        juce::dsp::AudioBlock<const float> block (buf);
        a.push (block);
    }
}

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

} // namespace

TEST_CASE ("Analyser reads a full-scale sine at 0 dBFS in its own band", "[spectrum]")
{
    dsp::SpectrumAnalyser a;
    a.prepare (kFs);

    pushSine (a, 1000.0f, 1.0f, 0.5);

    SpectrumFrame frame;
    REQUIRE (a.render (frame));

    const int band = bandFor (1000.0f);
    CHECK (frame.db[static_cast<size_t> (band)] == Approx (0.0f).margin (1.0));

    // The peak must be in that band (or an immediate neighbour, since the tone
    // can sit near a band edge), not smeared across the spectrum.
    int loudest = 0;
    for (int b = 1; b < kSpectrumBands; ++b)
        if (frame.db[static_cast<size_t> (b)] > frame.db[static_cast<size_t> (loudest)])
            loudest = b;
    CHECK (std::abs (loudest - band) <= 1);

    // Two octaves down should be far below the tone.
    CHECK (frame.db[static_cast<size_t> (bandFor (250.0f))] < -40.0f);
}

TEST_CASE ("Analyser level tracks input amplitude in dB", "[spectrum]")
{
    dsp::SpectrumAnalyser a;
    a.prepare (kFs);

    pushSine (a, 1000.0f, 0.5f, 0.5);      // -6.02 dBFS

    SpectrumFrame frame;
    REQUIRE (a.render (frame));
    CHECK (frame.db[static_cast<size_t> (bandFor (1000.0f))] == Approx (-6.02f).margin (1.0));
}

TEST_CASE ("Analyser resolves two tones into separate bands", "[spectrum]")
{
    dsp::SpectrumAnalyser a;
    a.prepare (kFs);

    // Sum of 100 Hz and 5 kHz, each at -12 dBFS.
    juce::AudioBuffer<float> buf (2, kBlock);
    int n = 0;
    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < kBlock; ++i, ++n)
        {
            const auto t = 2.0f * juce::MathConstants<float>::pi * static_cast<float> (n)
                           / static_cast<float> (kFs);
            const float v = 0.25f * (std::sin (t * 100.0f) + std::sin (t * 5000.0f));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        juce::dsp::AudioBlock<const float> block (buf);
        a.push (block);
    }

    SpectrumFrame frame;
    REQUIRE (a.render (frame));

    const auto low  = frame.db[static_cast<size_t> (bandFor (100.0f))];
    const auto high = frame.db[static_cast<size_t> (bandFor (5000.0f))];
    const auto mid  = frame.db[static_cast<size_t> (bandFor (800.0f))];

    CHECK (low  == Approx (-12.0f).margin (1.5));
    CHECK (high == Approx (-12.0f).margin (1.5));
    CHECK (mid < low - 30.0f);      // the gap between them is empty
    CHECK (mid < high - 30.0f);
}

TEST_CASE ("Analyser decays towards the floor when the audio stops", "[spectrum]")
{
    dsp::SpectrumAnalyser a;
    a.prepare (kFs);

    pushSine (a, 1000.0f, 1.0f, 0.5);

    SpectrumFrame frame;
    REQUIRE (a.render (frame));
    const auto peak = frame.db[static_cast<size_t> (bandFor (1000.0f))];

    // The transport stops: no further push(), so render() must fade rather
    // than freeze on the last spectrum.
    for (int i = 0; i < 5; ++i)
        REQUIRE (a.render (frame));

    const auto faded = frame.db[static_cast<size_t> (bandFor (1000.0f))];
    CHECK (faded < peak - 10.0f);

    for (int i = 0; i < 100; ++i)
        a.render (frame);
    CHECK (frame.db[static_cast<size_t> (bandFor (1000.0f))] < kSpectrumFloorDb + 1.0f);
}

TEST_CASE ("Analyser reports silence at the floor", "[spectrum]")
{
    dsp::SpectrumAnalyser a;
    a.prepare (kFs);

    pushSine (a, 1000.0f, 0.0f, 0.5);      // silence, but the ring is advancing

    SpectrumFrame frame;
    REQUIRE (a.render (frame));

    for (int b = 0; b < kSpectrumBands; ++b)
        CHECK (frame.db[static_cast<size_t> (b)] == Approx (kSpectrumFloorDb).margin (0.01));
}

TEST_CASE ("Analyser publishes nothing before prepare", "[spectrum]")
{
    dsp::SpectrumAnalyser a;
    SpectrumFrame frame;
    CHECK_FALSE (a.render (frame));
}

TEST_CASE ("EQ-6 taps its RTA after the filters, not before", "[spectrum][eq]")
{
    // A deep, narrow cut at 1 kHz. If the analyser were tapped pre-EQ the
    // spectrum would be unchanged; post-EQ it must show the notch.
    Params p;
    for (int i = 0; i < EqModule::kBands; ++i)
        p.set ("b" + juce::String (i) + "on", 0.0f);

    p.set ("b3on", 1.0f);
    p.set ("b3type", 0.0f);          // Bell
    p.set ("b3freq", 1000.0f);
    p.set ("b3gain", -18.0f);
    p.set ("b3q", 4.0f);

    EqModule eq;
    p.bindAll (eq);
    eq.prepare (kFs, kBlock, 2);

    juce::AudioBuffer<float> buf (2, kBlock);
    int n = 0;
    for (int b = 0; b < 60; ++b)
    {
        for (int i = 0; i < kBlock; ++i, ++n)
        {
            const auto t = 2.0f * juce::MathConstants<float>::pi * static_cast<float> (n)
                           / static_cast<float> (kFs);
            // 1 kHz (cut) plus a 200 Hz reference the EQ leaves alone.
            const float v = 0.25f * (std::sin (t * 1000.0f) + std::sin (t * 200.0f));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        juce::dsp::AudioBlock<float> block (buf);
        eq.process (block, ProcessContext {});
    }

    SpectrumFrame frame;
    REQUIRE (eq.readSpectrum (frame));

    const auto cut       = frame.db[static_cast<size_t> (bandFor (1000.0f))];
    const auto reference = frame.db[static_cast<size_t> (bandFor (200.0f))];

    CHECK (reference == Approx (-12.0f).margin (1.5));   // untouched band
    CHECK (cut < reference - 12.0f);                     // the notch is visible
}

TEST_CASE ("Modules without an analyser publish no spectrum", "[spectrum]")
{
    // The default AudioModule implementation must opt out, so the editor never
    // ships an empty spectrum for, say, a Gain slot.
    struct Silent final : AudioModule
    {
        ModuleDescriptor descriptor() const override { return { "t", "T", "Test", 1 }; }
        void prepare (double, int, int) override {}
        void reset() override {}
        void bindParameter (const juce::String&, std::atomic<float>*) override {}
        void process (juce::dsp::AudioBlock<float>&, const ProcessContext&) noexcept override {}
    };

    Silent s;
    SpectrumFrame frame;
    CHECK_FALSE (s.readSpectrum (frame));
}
