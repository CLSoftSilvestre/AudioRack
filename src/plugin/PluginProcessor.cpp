#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace audiorack
{

juce::AudioProcessor::BusesProperties AudioRackProcessor::makeBusesProperties()
{
    return BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false);
}

AudioRackProcessor::AudioRackProcessor()
    : juce::AudioProcessor (makeBusesProperties())
{
}

void AudioRackProcessor::prepareToPlay (double, int)
{
    // M1 will forward this to the RackEngine. Nothing to preallocate yet.
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

void AudioRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Passthrough: leave the main bus untouched, silence any output channels
    // that have no corresponding input.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* AudioRackProcessor::createEditor()
{
    return new AudioRackEditor (*this);
}

void AudioRackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Schema-versioned from the start so M2's real state format can migrate.
    juce::ValueTree state ("AudioRack");
    state.setProperty ("schemaVersion", 1, nullptr);

    juce::MemoryOutputStream stream (destData, false);
    state.writeToStream (stream);
}

void AudioRackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));
    juce::ignoreUnused (state); // M2 restores rack + parameters from here.
}

} // namespace audiorack

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new audiorack::AudioRackProcessor();
}
