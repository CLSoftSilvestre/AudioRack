#include "PluginEditor.h"

namespace audiorack
{

namespace
{
    // Browser panel (210) + a comfortably scaled rack. Kept clear of a 1280-wide
    // laptop screen so the initial window is not clamped narrower than the rack
    // (the UI now fills any width, but a fresh window should still open unclipped).
    constexpr int  kDefaultWidth  = 1200;
    constexpr int  kDefaultHeight = 760;
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
