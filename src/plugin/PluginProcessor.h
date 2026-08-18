#pragma once

#include <JuceHeader.h>

#include <array>
#include <map>
#include <utility>
#include <vector>

#include "../core/RackEngine.h"

namespace audiorack
{

class SessionRecovery;

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
    bool acceptsMidi() const override { return true; }
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

    /// Mounts a copy of fromSlot's module into (empty) toSlot, values and all.
    bool duplicateModule (int fromSlot, int toSlot);

    const juce::String& mountedModuleId (int slot) const noexcept;

    /// Broadcasts on every rack layout change (mount/unmount/move/preset load).
    juce::ChangeBroadcaster rackLayoutChanged;

    // --- A/B compare (message thread) ----------------------------------------
    //
    // Two parameter snapshots the user flips between. The rack layout is shared
    // (params only); only the *inactive* bank is stored explicitly — the active
    // bank is always the live APVTS state, captured lazily when it is left.

    int  activeBank() const noexcept { return currentBank; }

    /// Recall the other bank: stashes the live values into the current bank,
    /// then loads the requested bank into the live parameters.
    void selectBank (int bank);

    /// Copy the current (live) bank onto the other, so A and B start equal.
    void copyBankToOther();

    /// Broadcasts whenever the active bank or a bank's contents change.
    juce::ChangeBroadcaster abStateChanged;

    // State (de)serialisation reaches into the banks directly.
    void captureBank (int bank);
    const std::map<juce::String, float>& bankSnapshot (int bank) const noexcept
    {
        return banks[static_cast<size_t> (bank)];
    }
    void loadBankSnapshot (int bank, std::map<juce::String, float> snapshot);
    void setActiveBank (int bank) noexcept { currentBank = juce::jlimit (0, 1, bank); }

    // --- MIDI learn (message thread, except where noted) ----------------------
    //
    // A continuous controller (CC) can be mapped to any host parameter. The map
    // (CC -> parameter index) is an array of atomics: the audio thread only
    // reads it and stashes the newest value per parameter; the message-thread
    // timer applies those values (setValueNotifyingHost is not RT-safe) and owns
    // all mutation. Learning: arm a parameter, then the next CC seen on the
    // audio thread binds to it.

    void armMidiLearn (const juce::String& paramID);
    void cancelMidiLearn();
    void forgetMidiMapping (const juce::String& paramID);

    /// Empty when nothing is armed.
    juce::String midiArmedParamId() const;

    /// (CC number, parameter id) for every current mapping.
    std::vector<std::pair<int, juce::String>> midiMappings() const;

    // Used by state load.
    void clearAllMidiMappings();
    void setMidiMapping (int cc, const juce::String& paramID);

    /// Broadcasts when the armed parameter or the CC map changes.
    juce::ChangeBroadcaster midiStateChanged;

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
    void applyBank (int bank);

    void handleControlChange (int cc, int value) noexcept;   // audio thread
    void bindMidiCc (int cc, int paramIndex);                // message thread
    void drainMidi();                                        // message thread
    int  paramIndexFor (const juce::String& paramID) const;

    static BusesProperties makeBusesProperties();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    RackEngine engine;
    juce::AudioProcessorValueTreeState apvts;

    std::array<juce::String, kMaxSlots> slotModuleIds;

    std::array<std::map<juce::String, float>, 2> banks;   // value01 snapshots
    int currentBank = 0;

    // MIDI learn. Parameter index space == position in automatableParams.
    std::vector<juce::RangedAudioParameter*> automatableParams;
    std::map<juce::String, int>              paramIdToIndex;

    std::array<std::atomic<int>, 128> ccToParam;          // CC -> param index, -1 = unmapped
    std::atomic<int> learnArmed { -1 };                   // audio-visible arm flag
    std::atomic<int> pendingLearnedCc { -1 };             // audio -> message handoff
    int              messageArmedIndex = -1;              // message-thread authority

    std::unique_ptr<std::atomic<float>[]> midiValue;      // newest value per param
    std::unique_ptr<std::atomic<bool>[]>  midiDirty;

    std::atomic<int> editorWidth { 1200 }, editorHeight { 760 };

    // --- Crash-safe state (standalone only) ----------------------------------
    // The plugin's state is owned by the host, so recovery is null there. In the
    // standalone, the timer autosaves the full state atomically and, on a launch
    // that follows a crash, the recovered snapshot is applied once startup has
    // settled (see timerCallback / the constructor).
    std::unique_ptr<SessionRecovery> recovery;
    juce::String pendingRecovery;          // snapshot to apply on the first timer tick
    int          autosaveCountdown = 1;    // timer ticks until the next autosave

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRackProcessor)
};

} // namespace audiorack
