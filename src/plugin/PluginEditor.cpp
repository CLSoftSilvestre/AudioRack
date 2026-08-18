#include "PluginEditor.h"

namespace audiorack
{

namespace
{
    constexpr int  kDefaultWidth  = 1300;   // browser panel + full-width rack
    constexpr int  kDefaultHeight = 780;
    constexpr double kAspect      = static_cast<double> (kDefaultWidth) / kDefaultHeight;
}

AudioRackEditor::AudioRackEditor (AudioRackProcessor& p)
    : juce::AudioProcessorEditor (p),
      rackProcessor (p),
      webView (p)
{
    addAndMakeVisible (webView);

    setResizable (true, true);
    setResizeLimits (1000, static_cast<int> (1000 / kAspect), 2400, static_cast<int> (2400 / kAspect));

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
