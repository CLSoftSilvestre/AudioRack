#include "PluginEditor.h"

namespace audiorack
{

namespace
{
    constexpr int  kDefaultWidth  = 1100;
    constexpr int  kDefaultHeight = 740;
    constexpr double kAspect      = static_cast<double> (kDefaultWidth) / kDefaultHeight;
}

AudioRackEditor::AudioRackEditor (AudioRackProcessor& p)
    : juce::AudioProcessorEditor (p),
      rackProcessor (p),
      webView (p)
{
    addAndMakeVisible (webView);

    setResizable (true, true);
    setResizeLimits (770, static_cast<int> (770 / kAspect), 2200, static_cast<int> (2200 / kAspect));

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio (kAspect);

    const auto stored = rackProcessor.editorSize();
    setSize (stored.x > 0 ? stored.x : kDefaultWidth,
             stored.y > 0 ? stored.y : kDefaultHeight);
}

void AudioRackEditor::resized()
{
    webView.setBounds (getLocalBounds());
    rackProcessor.setEditorSize ({ getWidth(), getHeight() });
}

} // namespace audiorack
