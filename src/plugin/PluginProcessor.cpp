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

void AudioRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

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
