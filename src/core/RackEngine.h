#pragma once

#include "RackSlot.h"
#include "RackCommands.h"
#include "SpscQueue.h"

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

namespace audiorack
{

/// One meter snapshot tagged with its slot, shipped audio -> UI per block.
struct SlotMeterFrame
{
    int        slot = 0;
    MeterFrame frame;
};

/** Ordered serial chain of rack modules (v1 routing: strictly top-to-bottom).

    Threading model:
      - process() runs on the audio thread. It drains the command queue at the
        top of each block, applies structural changes, then processes slots in
        order with per-slot bypass/wet-dry crossfades. Realtime-safe: no
        allocation, locks, strings or exceptions.
      - setModule()/clearSlot()/moveModule() run on the message thread. They
        prepare modules *before* enqueueing, so the audio thread only ever
        mounts ready-to-run instances.
      - Displaced modules travel back on a disposal queue; collectGarbage()
        deletes them on the message thread (poll from a timer).
      - prepare() may consume the command queue itself: the host guarantees no
        concurrent processBlock during prepareToPlay, which is the only time
        it is called.
*/
class RackEngine
{
public:
    RackEngine() = default;
    ~RackEngine();

    // --- message thread -----------------------------------------------------

    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /// Mounts a module (already constructed) in a slot. Takes ownership.
    /// Returns false (and keeps your module alive via the return) on a full queue.
    bool setModule (int slot, std::unique_ptr<AudioModule> module);
    bool clearSlot (int slot);
    bool moveModule (int fromSlot, int toSlot);

    /// Deletes modules the audio thread has handed back. Call periodically.
    void collectGarbage();

    /// Message-thread mirror of what is (or is about to be) mounted.
    AudioModule* mountedModule (int slot) const noexcept { return mirror[static_cast<size_t> (slot)]; }

    RackSlot& slot (int index) noexcept { return slots[static_cast<size_t> (index)]; }

    // --- audio thread ---------------------------------------------------------

    void process (juce::dsp::AudioBlock<float>& block, const TransportInfo& transport) noexcept;

    /// Sum of mounted modules' latencies, updated on structural change.
    int latencySamples() const noexcept { return totalLatency.load (std::memory_order_relaxed); }

    /// UI thread: drains the meter ring buffer (poll at ~60 Hz and fold —
    /// take the max peak across drained frames so short peaks between polls
    /// are never lost, even if a UI frame is dropped).
    bool popMeterFrame (SlotMeterFrame& out) noexcept { return meters.pop (out); }

private:
    void applyCommand (const RackCommand&) noexcept;
    void discard (AudioModule*) noexcept;
    void updateLatency() noexcept;

    std::array<RackSlot, kMaxSlots>     slots;
    std::array<AudioModule*, kMaxSlots> mirror {};

    SpscQueue<RackCommand, 64>   commands;   // message -> audio
    SpscQueue<AudioModule*, 256> disposal;   // audio -> message; sized so it cannot fill
                                             // (each command displaces at most one module)
    SpscQueue<SlotMeterFrame, 4096> meters;  // audio -> UI; overflow drops frames, which the
                                             // 60 Hz drain makes practically unreachable

    juce::AudioBuffer<float> dryBuffer;      // preallocated wet/dry scratch

    std::atomic<int> totalLatency { 0 };

    double sampleRate   = 44100.0;
    int    maxBlockSize = 0;
    int    numChannels  = 0;
};

} // namespace audiorack
