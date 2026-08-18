#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "../core/ModuleRegistry.h"
#include "../state/RackState.h"

namespace audiorack
{

juce::AudioProcessor::BusesProperties AudioRackProcessor::makeBusesProperties()
{
    return BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false);
}

juce::AudioProcessorValueTreeState::ParameterLayout AudioRackProcessor::createParameterLayout()
{
    registerBuiltinModules();

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int slot = 0; slot < kMaxSlots; ++slot)
    {
        const auto slotLabel = "Slot " + juce::String (slot + 1);

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { makeSlotParamID (slot, "bypass"), 1 },
            slotLabel + " Bypass", false));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { makeSlotParamID (slot, "mix"), 1 },
            slotLabel + " Mix",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

        for (const auto& type : ModuleRegistry::instance().types())
        {
            ParameterBuilder builder;
            type.declareParameters (builder);

            for (const auto& spec : builder.specs())
            {
                const juce::ParameterID id { makeParamID (slot, type.descriptor.id, spec.idSuffix), 1 };
                const auto name = slotLabel + " " + type.descriptor.name + " " + spec.displayName;

                if (spec.isChoice())
                    layout.add (std::make_unique<juce::AudioParameterChoice> (
                        id, name, spec.choices, static_cast<int> (spec.defaultValue)));
                else
                    layout.add (std::make_unique<juce::AudioParameterFloat> (
                        id, name, spec.range, spec.defaultValue,
                        juce::AudioParameterFloatAttributes().withLabel (spec.unit)));
            }
        }
    }

    return layout;
}

AudioRackProcessor::AudioRackProcessor()
    : juce::AudioProcessor (makeBusesProperties()),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    for (int slot = 0; slot < kMaxSlots; ++slot)
    {
        engine.slot (slot).bindBypass (apvts.getRawParameterValue (makeSlotParamID (slot, "bypass")));
        engine.slot (slot).bindMix    (apvts.getRawParameterValue (makeSlotParamID (slot, "mix")));
    }

    // Fresh instances start with a Gain in the first slot so there is
    // something to see, hear and automate.
    mountModule (0, "gain");

    // Seed both A/B banks with the initial (default) parameter values.
    captureBank (0);
    captureBank (1);

    // Build the MIDI-learn parameter index space and clear the CC map.
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            paramIdToIndex[ranged->paramID] = static_cast<int> (automatableParams.size());
            automatableParams.push_back (ranged);
        }

    for (auto& slot : ccToParam)
        slot.store (-1, std::memory_order_relaxed);

    const auto numParams = automatableParams.size();
    midiValue = std::make_unique<std::atomic<float>[]> (numParams);
    midiDirty = std::make_unique<std::atomic<bool>[]> (numParams);

    for (size_t i = 0; i < numParams; ++i)
    {
        midiValue[i].store (0.0f,  std::memory_order_relaxed);
        midiDirty[i].store (false, std::memory_order_relaxed);
    }

    startTimerHz (30);
}

AudioRackProcessor::~AudioRackProcessor()
{
    stopTimer();
}

// --- Rack management ----------------------------------------------------------

bool AudioRackProcessor::mountModule (int slot, const juce::String& moduleId)
{
    jassert (juce::isPositiveAndBelow (slot, kMaxSlots));

    const auto* info = ModuleRegistry::instance().find (moduleId.toStdString());

    if (info == nullptr)
        return false;

    auto instance = info->create();

    ParameterBuilder builder;
    info->declareParameters (builder);

    for (const auto& spec : builder.specs())
        instance->bindParameter (spec.idSuffix,
                                 apvts.getRawParameterValue (makeParamID (slot, info->descriptor.id, spec.idSuffix)));

    if (! engine.setModule (slot, std::move (instance)))
        return false;

    slotModuleIds[static_cast<size_t> (slot)] = moduleId;
    rackLayoutChanged.sendChangeMessage();
    return true;
}

bool AudioRackProcessor::unmountSlot (int slot)
{
    jassert (juce::isPositiveAndBelow (slot, kMaxSlots));

    if (slotModuleIds[static_cast<size_t> (slot)].isEmpty())
        return true;

    if (! engine.clearSlot (slot))
        return false;

    slotModuleIds[static_cast<size_t> (slot)].clear();
    rackLayoutChanged.sendChangeMessage();
    return true;
}

bool AudioRackProcessor::moveModule (int fromSlot, int toSlot)
{
    jassert (juce::isPositiveAndBelow (fromSlot, kMaxSlots));
    jassert (juce::isPositiveAndBelow (toSlot, kMaxSlots));

    if (fromSlot == toSlot)
        return true;

    const auto movingId  = slotModuleIds[static_cast<size_t> (fromSlot)];
    const auto displaced = slotModuleIds[static_cast<size_t> (toSlot)];

    if (movingId.isEmpty())
        return false;

    // Mounted modules never rebind parameters (that would race the audio
    // thread), so a move is: copy values across slots, remount fresh.
    copyModuleParams (fromSlot, toSlot, movingId);

    if (displaced.isNotEmpty())
        copyModuleParams (toSlot, fromSlot, displaced);

    mountModule (toSlot, movingId);

    if (displaced.isNotEmpty())
        mountModule (fromSlot, displaced);
    else
        unmountSlot (fromSlot);

    return true;
}

bool AudioRackProcessor::duplicateModule (int fromSlot, int toSlot)
{
    jassert (juce::isPositiveAndBelow (fromSlot, kMaxSlots));
    jassert (juce::isPositiveAndBelow (toSlot, kMaxSlots));

    const auto sourceId = slotModuleIds[static_cast<size_t> (fromSlot)];

    if (sourceId.isEmpty() || slotModuleIds[static_cast<size_t> (toSlot)].isNotEmpty())
        return false;

    copyModuleParams (fromSlot, toSlot, sourceId);
    return mountModule (toSlot, sourceId);
}

void AudioRackProcessor::copyModuleParams (int fromSlot, int toSlot, const juce::String& moduleId)
{
    const auto* info = ModuleRegistry::instance().find (moduleId.toStdString());

    if (info == nullptr)
        return;

    ParameterBuilder builder;
    info->declareParameters (builder);

    auto copyOne = [this] (const juce::String& fromID, const juce::String& toID)
    {
        auto* from = apvts.getParameter (fromID);
        auto* to   = apvts.getParameter (toID);

        if (from != nullptr && to != nullptr)
            to->setValueNotifyingHost (from->getValue());
    };

    for (const auto& spec : builder.specs())
        copyOne (makeParamID (fromSlot, info->descriptor.id, spec.idSuffix),
                 makeParamID (toSlot,   info->descriptor.id, spec.idSuffix));

    copyOne (makeSlotParamID (fromSlot, "bypass"), makeSlotParamID (toSlot, "bypass"));
    copyOne (makeSlotParamID (fromSlot, "mix"),    makeSlotParamID (toSlot, "mix"));
}

const juce::String& AudioRackProcessor::mountedModuleId (int slot) const noexcept
{
    return slotModuleIds[static_cast<size_t> (slot)];
}

// --- A/B compare --------------------------------------------------------------

void AudioRackProcessor::captureBank (int bank)
{
    auto& snapshot = banks[static_cast<size_t> (bank)];
    snapshot.clear();

    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            snapshot[ranged->paramID] = ranged->getValue();   // normalised 0..1
}

void AudioRackProcessor::applyBank (int bank)
{
    const auto& snapshot = banks[static_cast<size_t> (bank)];

    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (const auto it = snapshot.find (ranged->paramID); it != snapshot.end())
                ranged->setValueNotifyingHost (it->second);
}

void AudioRackProcessor::loadBankSnapshot (int bank, std::map<juce::String, float> snapshot)
{
    banks[static_cast<size_t> (bank)] = std::move (snapshot);
}

void AudioRackProcessor::selectBank (int bank)
{
    bank = juce::jlimit (0, 1, bank);

    if (bank == currentBank)
        return;

    captureBank (currentBank);   // remember the live values we are leaving
    currentBank = bank;
    applyBank (currentBank);     // recall the other bank's stored values
    abStateChanged.sendChangeMessage();
}

void AudioRackProcessor::copyBankToOther()
{
    captureBank (currentBank);
    banks[static_cast<size_t> (1 - currentBank)] = banks[static_cast<size_t> (currentBank)];
    abStateChanged.sendChangeMessage();
}

// --- MIDI learn ---------------------------------------------------------------

int AudioRackProcessor::paramIndexFor (const juce::String& paramID) const
{
    const auto it = paramIdToIndex.find (paramID);
    return it != paramIdToIndex.end() ? it->second : -1;
}

void AudioRackProcessor::handleControlChange (int cc, int value) noexcept
{
    if (! juce::isPositiveAndBelow (cc, 128))
        return;

    // Armed for learn: hand the CC to the message thread and disarm.
    if (learnArmed.load (std::memory_order_acquire) >= 0)
    {
        pendingLearnedCc.store (cc, std::memory_order_release);
        learnArmed.store (-1, std::memory_order_release);
        return;
    }

    const int idx = ccToParam[static_cast<size_t> (cc)].load (std::memory_order_acquire);

    if (idx >= 0)
    {
        // Coalesce: keep only the newest value; the timer applies it.
        midiValue[static_cast<size_t> (idx)].store (static_cast<float> (value) / 127.0f,
                                                    std::memory_order_relaxed);
        midiDirty[static_cast<size_t> (idx)].store (true, std::memory_order_release);
    }
}

void AudioRackProcessor::bindMidiCc (int cc, int paramIndex)
{
    if (! juce::isPositiveAndBelow (cc, 128))
        return;

    // A parameter is driven by at most one CC: release any previous binding.
    for (auto& slot : ccToParam)
        if (slot.load (std::memory_order_relaxed) == paramIndex)
            slot.store (-1, std::memory_order_release);

    ccToParam[static_cast<size_t> (cc)].store (paramIndex, std::memory_order_release);
}

void AudioRackProcessor::drainMidi()
{
    if (const int cc = pendingLearnedCc.exchange (-1, std::memory_order_acquire); cc >= 0)
    {
        if (messageArmedIndex >= 0)
        {
            bindMidiCc (cc, messageArmedIndex);
            messageArmedIndex = -1;
            midiStateChanged.sendChangeMessage();
        }
    }

    for (size_t i = 0; i < automatableParams.size(); ++i)
        if (midiDirty[i].exchange (false, std::memory_order_acquire))
            automatableParams[i]->setValueNotifyingHost (
                midiValue[i].load (std::memory_order_relaxed));
}

void AudioRackProcessor::armMidiLearn (const juce::String& paramID)
{
    const int idx = paramIndexFor (paramID);

    if (idx < 0)
        return;

    messageArmedIndex = idx;
    learnArmed.store (idx, std::memory_order_release);
    midiStateChanged.sendChangeMessage();
}

void AudioRackProcessor::cancelMidiLearn()
{
    messageArmedIndex = -1;
    learnArmed.store (-1, std::memory_order_release);
    midiStateChanged.sendChangeMessage();
}

void AudioRackProcessor::forgetMidiMapping (const juce::String& paramID)
{
    const int idx = paramIndexFor (paramID);

    if (idx < 0)
        return;

    for (auto& slot : ccToParam)
        if (slot.load (std::memory_order_relaxed) == idx)
            slot.store (-1, std::memory_order_release);

    midiStateChanged.sendChangeMessage();
}

juce::String AudioRackProcessor::midiArmedParamId() const
{
    if (messageArmedIndex >= 0 && messageArmedIndex < static_cast<int> (automatableParams.size()))
        return automatableParams[static_cast<size_t> (messageArmedIndex)]->paramID;

    return {};
}

std::vector<std::pair<int, juce::String>> AudioRackProcessor::midiMappings() const
{
    std::vector<std::pair<int, juce::String>> out;

    for (int cc = 0; cc < 128; ++cc)
    {
        const int idx = ccToParam[static_cast<size_t> (cc)].load (std::memory_order_relaxed);

        if (idx >= 0 && idx < static_cast<int> (automatableParams.size()))
            out.emplace_back (cc, automatableParams[static_cast<size_t> (idx)]->paramID);
    }

    return out;
}

void AudioRackProcessor::clearAllMidiMappings()
{
    for (auto& slot : ccToParam)
        slot.store (-1, std::memory_order_release);
}

void AudioRackProcessor::setMidiMapping (int cc, const juce::String& paramID)
{
    const int idx = paramIndexFor (paramID);

    if (idx >= 0)
        bindMidiCc (cc, idx);
}

// --- Audio ----------------------------------------------------------------------

void AudioRackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getMainBusNumOutputChannels());
    setLatencySamples (engine.latencySamples());
}

void AudioRackProcessor::releaseResources()
{
}

bool AudioRackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo())
        return false;

    const auto sidechain = layouts.getChannelSet (true, 1);

    return sidechain.isDisabled()
        || sidechain == juce::AudioChannelSet::mono()
        || sidechain == juce::AudioChannelSet::stereo();
}

TransportInfo AudioRackProcessor::currentTransport() noexcept
{
    TransportInfo info;
    info.sampleRate = getSampleRate();

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
                info.bpm = *bpm;

            if (const auto ppq = position->getPpqPosition())
                info.ppqPosition = *ppq;

            if (const auto sig = position->getTimeSignature())
            {
                info.timeSigNumerator   = sig->numerator;
                info.timeSigDenominator = sig->denominator;
            }

            info.isPlaying = position->getIsPlaying();
        }
    }

    return info;
}

void AudioRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI learn: raw-parse control-change messages (allocation-free, no
    // MidiMessage construction). Status 0xB0 | channel, data1 = CC, data2 = value.
    for (const auto metadata : midiMessages)
        if (metadata.numBytes == 3 && (metadata.data[0] & 0xF0) == 0xB0)
            handleControlChange (metadata.data[1], metadata.data[2]);

    // Silence any output channels that have no corresponding input.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    auto mainBus = getBusBuffer (buffer, false, 0);
    juce::dsp::AudioBlock<float> block (mainBus);

    ProcessContext context;
    context.transport = currentTransport();

    if (const auto* sidechainBus = getBus (true, 1);
        sidechainBus != nullptr && sidechainBus->isEnabled())
    {
        const auto sidechain = getBusBuffer (buffer, true, 1);

        if (sidechain.getNumChannels() > 0)
        {
            context.sidechain            = sidechain.getArrayOfReadPointers();
            context.numSidechainChannels = sidechain.getNumChannels();
            context.numSidechainSamples  = sidechain.getNumSamples();
        }
    }

    engine.process (block, context);
}

void AudioRackProcessor::timerCallback()
{
    engine.collectGarbage();
    drainMidi();

    const auto latency = engine.latencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

juce::AudioProcessorEditor* AudioRackProcessor::createEditor()
{
    return new AudioRackEditor (*this);
}

// --- State ------------------------------------------------------------------------

void AudioRackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    captureBank (currentBank);   // fold live values into the active bank first
    const auto json = rackStateToJson (*this);
    destData.replaceAll (json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void AudioRackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto json = juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes);
    applyRackStateJson (*this, json);
}

} // namespace audiorack

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new audiorack::AudioRackProcessor();
}
