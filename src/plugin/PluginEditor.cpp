#include "PluginEditor.h"

namespace audiorack
{

AudioRackEditor::AudioRackEditor (AudioRackProcessor& p)
    : juce::AudioProcessorEditor (p)
{
    setSize (480, 200);
}

void AudioRackEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1c1e));

    g.setColour (juce::Colour (0xffd6d9dc));
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("AudioRack", getLocalBounds().removeFromTop (getHeight() / 2),
                juce::Justification::centredBottom);

    g.setColour (juce::Colour (0xff7a8288));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("M0 skeleton - audio passthrough",
                getLocalBounds().removeFromBottom (getHeight() / 2),
                juce::Justification::centredTop);
}

} // namespace audiorack
