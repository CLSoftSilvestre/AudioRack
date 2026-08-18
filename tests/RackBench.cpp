// Audio-thread performance pass for the M8 shipping milestone.
//
// Builds a fully loaded 12-slot rack (every registered module type, cycled to
// fill all kMaxSlots), then times RackEngine::process over many blocks and
// reports the per-block wall-clock distribution against the realtime budget
// (blockSize / sampleRate). This is a measurement tool, not a pass/fail test —
// it is deliberately kept out of ctest because absolute timings are
// machine-dependent. Run it to produce the numbers quoted in docs/PROGRESS.md.
//
// Build (Release): cmake --build --preset macos --target audiorack_bench
// Run:            ./build/macos/tests/audiorack_bench [blockSize] [sampleRate]

#include <core/ModuleRegistry.h>
#include <core/RackEngine.h>

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <random>
#include <vector>

using namespace audiorack;

int main (int argc, char** argv)
{
    const int    blockSize  = argc > 1 ? std::atoi (argv[1]) : 512;
    const double sampleRate = argc > 2 ? std::atof (argv[2]) : 48000.0;
    const int    channels   = 2;

    registerBuiltinModules();
    auto& registry = ModuleRegistry::instance();
    const auto& types = registry.types();

    RackEngine engine;
    engine.prepare (sampleRate, blockSize, channels);

    // Parameter atomics must outlive the run: modules hold raw pointers into it.
    std::vector<std::unique_ptr<std::atomic<float>>> paramStore;

    // Fill every slot, cycling through all module types so the chain is a
    // representative "full rack" rather than 12 copies of one cheap module.
    for (int slot = 0; slot < kMaxSlots; ++slot)
    {
        const auto& info = types[static_cast<size_t> (slot) % types.size()];
        auto module = info.create();

        ParameterBuilder pb;
        info.declareParameters (pb);
        for (const auto& spec : pb.specs())
        {
            paramStore.push_back (std::make_unique<std::atomic<float>> (spec.defaultValue));
            module->bindParameter (spec.idSuffix, paramStore.back().get());
        }

        engine.setModule (slot, std::move (module));
    }

    juce::AudioBuffer<float>     buffer (channels, blockSize);
    juce::dsp::AudioBlock<float> block (buffer);

    ProcessContext ctx;
    ctx.transport.sampleRate = sampleRate;
    ctx.transport.bpm        = 120.0;
    ctx.transport.isPlaying  = true;

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
    auto fillNoise = [&]
    {
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, dist (rng));
    };

    juce::ScopedNoDenormals noDenormals;

    // Warm-up: drains the mount commands, settles filter/reverb state, warms
    // the instruction/data caches. Not timed.
    for (int i = 0; i < 400; ++i)
    {
        fillNoise();
        engine.process (block, ctx);
    }

    const int N = 30000;
    std::vector<double> micros;
    micros.reserve (static_cast<size_t> (N));

    for (int i = 0; i < N; ++i)
    {
        fillNoise();   // outside the timed region
        const auto t0 = std::chrono::steady_clock::now();
        engine.process (block, ctx);
        const auto t1 = std::chrono::steady_clock::now();
        micros.push_back (std::chrono::duration<double, std::micro> (t1 - t0).count());
    }

    std::sort (micros.begin(), micros.end());
    const auto at = [&] (double p) { return micros[static_cast<size_t> (p * (N - 1))]; };

    const double budgetUs = static_cast<double> (blockSize) / sampleRate * 1.0e6;
    const double meanUs   = std::accumulate (micros.begin(), micros.end(), 0.0)
                            / static_cast<double> (N);
    const auto asPct = [&] (double us) { return 100.0 * us / budgetUs; };

    std::printf ("\nAudioRack — full %d-slot rack (%zu module types cycled)\n",
                 kMaxSlots, types.size());
    std::printf ("  sample rate %.0f Hz, block %d samples, %d blocks timed\n",
                 sampleRate, blockSize, N);
    std::printf ("  realtime budget per block: %.1f us (one CPU core)\n\n", budgetUs);

    std::printf ("  %-8s %10s %9s\n", "stat", "time", "of budget");
    std::printf ("  %-8s %8.2f us %7.2f %%\n", "mean", meanUs, asPct (meanUs));
    std::printf ("  %-8s %8.2f us %7.2f %%\n", "p50",  at (0.50), asPct (at (0.50)));
    std::printf ("  %-8s %8.2f us %7.2f %%\n", "p95",  at (0.95), asPct (at (0.95)));
    std::printf ("  %-8s %8.2f us %7.2f %%\n", "p99",  at (0.99), asPct (at (0.99)));
    std::printf ("  %-8s %8.2f us %7.2f %%\n", "max",  micros.back(), asPct (micros.back()));
    std::printf ("\n  reported latency: %d samples\n\n", engine.latencySamples());

    return 0;
}
