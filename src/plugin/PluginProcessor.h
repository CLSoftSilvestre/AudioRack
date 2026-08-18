#pragma once

#include <JuceHeader.h>

#include "../core/RackEngine.h"

namespace audiorack
{

/** AudioRack plugin processor.

    Hosts the RackEngine (serial module chain) and owns the APVTS. Parameters
    for every slot x registered module type are declared up front (APVTS
    layouts are fixed at construction); a mounted module binds to its slot's
    atomics, everything else lies dormant. IDs follow "slotN.<module>.<param>"
    plus per-slot "slotN.bypass" / "slotN.mix".

    The external sidechain input bus is declared from day one so the
    host-visible bus layout never changes once consumers (compressor, gate)
    arrive.

    A 30 Hz message-thread timer reclaims modules the audio thread has
    unmounted and keeps the host's latency figure in sync with the engine.
*/
class AudioRackProcessor final : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    AudioRackProcessor();
    ~AudioRackProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- Rack management (message thread) ------------------------------------

    /// Creates, binds and mounts a module type in a slot. False on unknown id.
    bool mountModule (int slot, const juce::String& moduleId);
    bool unmountSlot (int slot);

    /// Reorder. Occupied destination = swap. Module parameter *values* travel
    /// with the module; DSP runtime state (tails etc.) deliberately resets.
    bool moveModule (int fromSlot, int toSlot);

    const juce::String& mountedModuleId (int slot) const noexcept;

    /// Broadcasts on every rack layout change (mount/unmount/move/preset load).
    juce::ChangeBroadcaster rackLayoutChanged;

    // --- Accessors ------------------------------------------------------------

    RackEngine& rackEngine() noexcept { return engine; }
    juce::AudioProcessorValueTreeState&       parameterState() noexcept       { return apvts; }
    const juce::AudioProcessorValueTreeState& parameterState() const noexcept { return apvts; }

    juce::Point<int> editorSize() const noexcept { return { editorWidth.load(), editorHeight.load() }; }
    void setEditorSize (juce::Point<int> size) noexcept
    {
        editorWidth.store (size.x);
        editorHeight.store (size.y);
    }

private:
    void timerCallback() override;
    TransportInfo currentTransport() noexcept;
    void copyModuleParams (int fromSlot, int toSlot, const juce::String& moduleId);

    static BusesProperties makeBusesProperties();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    RackEngine engine;
    juce::AudioProcessorValueTreeState apvts;

    std::array<juce::String, kMaxSlots> slotModuleIds;

    std::atomic<int> editorWidth { 1100 }, editorHeight { 740 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRackProcessor)
};

} // namespace audiorack
