#pragma once

#include "PluginProcessor.h"

namespace audiorack
{

/** Placeholder editor for M0. Replaced by the WebView UI shell at M3. */
class AudioRackEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudioRackEditor (AudioRackProcessor&);

    void paint (juce::Graphics&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRackEditor)
};

} // namespace audiorack
