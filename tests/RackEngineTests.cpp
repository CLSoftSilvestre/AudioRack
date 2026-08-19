#include <core/RackEngine.h>
#include <core/ModuleRegistry.h>
#include <dsp/gain/GainModule.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <atomic>
#include <cmath>
#include <memory>

using namespace audiorack;
using Catch::Approx;

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;

struct TestModule final : AudioModule
{
    static inline int aliveCount = 0;

    explicit TestModule (int latency = 0) : reportedLatency (latency) { ++aliveCount; }
    ~TestModule() override { --aliveCount; }

    ModuleDescriptor descriptor() const override { return { "test", "Test", "Test", 1 }; }
    void prepare (double, int, int) override {}
    void reset() override {}
    void bindParameter (const juce::String&, std::atomic<float>*) override {}
    void process (juce::dsp::AudioBlock<float>&, const ProcessContext&) noexcept override {}
    int latencySamples() const noexcept override { return reportedLatency; }

    int reportedLatency = 0;
};

/// Fills a stereo buffer with DC 1.0 and runs it through the engine.
juce::AudioBuffer<float> processDcBlock (RackEngine& engine)
{
    juce::AudioBuffer<float> buffer (2, kBlockSize);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlockSize; ++i)
            buffer.setSample (ch, i, 1.0f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block, {});
    return buffer;
}

} // namespace

TEST_CASE ("Registry knows the gain module", "[registry]")
{
    registerBuiltinModules();

    auto& registry = ModuleRegistry::instance();
    REQUIRE (registry.find ("gain") != nullptr);
    REQUIRE (registry.find ("nope") == nullptr);

    auto instance = registry.createInstance ("gain");
    REQUIRE (instance != nullptr);
    CHECK (instance->descriptor().rackUnits == 1);

    ParameterBuilder builder;
    registry.find ("gain")->declareParameters (builder);
    REQUIRE (builder.specs().size() == 2);
    CHECK (builder.specs()[0].idSuffix == "gaindb");
    CHECK (builder.specs()[1].idSuffix == "chmode");
    CHECK (builder.specs()[1].isChoice());
    CHECK (makeParamID (3, "gain", builder.specs()[0].idSuffix) == "slot3.gain.gaindb");
}

TEST_CASE ("Empty engine passes audio through untouched", "[engine]")
{
    RackEngine engine;
    engine.prepare (kSampleRate, kBlockSize, 2);

    const auto buffer = processDcBlock (engine);

    CHECK (buffer.getSample (0, 0) == 1.0f);
    CHECK (buffer.getSample (1, kBlockSize - 1) == 1.0f);
    CHECK (engine.latencySamples() == 0);
}

TEST_CASE ("Mounted gain module applies its parameter", "[engine][gain]")
{
    registerBuiltinModules();

    RackEngine engine;
    engine.prepare (kSampleRate, kBlockSize, 2);

    std::atomic<float> gainDb { -6.0206f };   // 0.5 linear

    auto module = ModuleRegistry::instance().createInstance ("gain");
    module->bindParameter ("gaindb", &gainDb);
    REQUIRE (engine.setModule (0, std::move (module)));

    // Let the 10 ms parameter ramp settle (10 ms @ 48k = 480 samples < 1 block,
    // but the first block starts from the bound value anyway; run a few).
    juce::AudioBuffer<float> last;
    for (int i = 0; i < 5; ++i)
        last = processDcBlock (engine);

    CHECK (last.getSample (0, kBlockSize - 1) == Approx (0.5f).margin (1e-3));
    CHECK (last.getSample (1, kBlockSize - 1) == Approx (0.5f).margin (1e-3));

    // Meters saw the post-gain signal.
    MeterFrame frame;
    engine.slot (0).module->getMeterFrame (frame);
    CHECK (frame.peakL == Approx (0.5f).margin (1e-3));
    CHECK (frame.rmsL  == Approx (0.5f).margin (1e-3));
}

TEST_CASE ("Bypass crossfades without clicks", "[engine]")
{
    registerBuiltinModules();

    RackEngine engine;
    engine.prepare (kSampleRate, kBlockSize, 2);

    std::atomic<float> gainDb { 12.0f };      // ~3.98 linear: bypass step would be huge
    std::atomic<float> bypass { 0.0f };

    auto module = ModuleRegistry::instance().createInstance ("gain");
    module->bindParameter ("gaindb", &gainDb);
    engine.slot (0).bindBypass (&bypass);
    REQUIRE (engine.setModule (0, std::move (module)));

    for (int i = 0; i < 5; ++i)
        (void) processDcBlock (engine);

    bypass.store (1.0f);

    // Across the bypass transition the output must ramp, never jump. On DC
    // input the biggest legal per-sample step is the 20 ms ramp increment
    // (~gain delta / (0.02 * fs)) — allow generous headroom over that.
    float previous = juce::Decibels::decibelsToGain (12.0f);
    float maxStep  = 0.0f;
    bool  reachedDry = false;

    for (int b = 0; b < 6; ++b)
    {
        const auto out = processDcBlock (engine);

        for (int i = 0; i < kBlockSize; ++i)
        {
            const float v = out.getSample (0, i);
            maxStep  = juce::jmax (maxStep, std::abs (v - previous));
            previous = v;
        }
    }

    reachedDry = std::abs (previous - 1.0f) < 1e-4f;

    CHECK (reachedDry);
    CHECK (maxStep < (juce::Decibels::decibelsToGain (12.0f) - 1.0f) / (0.02f * (float) kSampleRate) * 4.0f);
}

TEST_CASE ("Latency sums across mounted modules and survives moves", "[engine]")
{
    RackEngine engine;
    engine.prepare (kSampleRate, kBlockSize, 2);

    REQUIRE (engine.setModule (2, std::make_unique<TestModule> (7)));
    REQUIRE (engine.setModule (5, std::make_unique<TestModule> (5)));

    (void) processDcBlock (engine);   // audio thread applies the commands
    CHECK (engine.latencySamples() == 12);

    REQUIRE (engine.moveModule (2, 3));
    (void) processDcBlock (engine);
    CHECK (engine.latencySamples() == 12);
    CHECK (engine.mountedModule (2) == nullptr);
    CHECK (engine.mountedModule (3) != nullptr);

    REQUIRE (engine.clearSlot (5));
    (void) processDcBlock (engine);
    CHECK (engine.latencySamples() == 7);
}

TEST_CASE ("Displaced modules are destroyed on the message thread only", "[engine]")
{
    TestModule::aliveCount = 0;

    {
        RackEngine engine;
        engine.prepare (kSampleRate, kBlockSize, 2);

        REQUIRE (engine.setModule (0, std::make_unique<TestModule>()));
        (void) processDcBlock (engine);
        CHECK (TestModule::aliveCount == 1);

        // Replace: old module is displaced, but must not die until collectGarbage.
        REQUIRE (engine.setModule (0, std::make_unique<TestModule>()));
        (void) processDcBlock (engine);
        CHECK (TestModule::aliveCount == 2);

        engine.collectGarbage();
        CHECK (TestModule::aliveCount == 1);
    }

    // Engine destruction reclaims whatever was still mounted.
    CHECK (TestModule::aliveCount == 0);
}

TEST_CASE ("Gain channel mode conditions a mono source", "[gain][channels]")
{
    GainModule gain;
    gain.prepare (kSampleRate, kBlockSize, 2);

    std::atomic<float> gainDb { 0.0f };   // unity gain, so output == conditioned input
    std::atomic<float> chmode { 0.0f };
    gain.bindParameter ("gaindb", &gainDb);
    gain.bindParameter ("chmode", &chmode);

    // Process one block with L=l, R=r at the given mode; report the last L/R.
    auto run = [&] (float mode, float l, float r, float& outL, float& outR)
    {
        chmode.store (mode);
        gain.reset();

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        for (int i = 0; i < kBlockSize; ++i)
        {
            buffer.setSample (0, i, l);
            buffer.setSample (1, i, r);
        }

        juce::dsp::AudioBlock<float> block (buffer);
        gain.process (block, {});

        outL = buffer.getSample (0, kBlockSize - 1);
        outR = buffer.getSample (1, kBlockSize - 1);
    };

    float l = 0.0f, r = 0.0f;

    SECTION ("Stereo passes channels through unchanged")
    {
        run (0.0f, 0.8f, 0.0f, l, r);
        CHECK (l == Approx (0.8f));
        CHECK (r == Approx (0.0f));
    }
    SECTION ("Left copies input 1 to both channels at full level")
    {
        run (2.0f, 0.8f, 0.0f, l, r);
        CHECK (l == Approx (0.8f));
        CHECK (r == Approx (0.8f));
    }
    SECTION ("Right copies input 2 to both channels")
    {
        run (3.0f, 0.0f, 0.5f, l, r);
        CHECK (l == Approx (0.5f));
        CHECK (r == Approx (0.5f));
    }
    SECTION ("Mono sums L+R at -6 dB")
    {
        run (1.0f, 0.8f, 0.0f, l, r);
        CHECK (l == Approx (0.4f));
        CHECK (r == Approx (0.4f));
    }
}
