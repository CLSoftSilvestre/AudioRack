#pragma once

#include "../ui/RackWebView.h"

namespace audiorack
{

/** Resizable editor hosting the WebView rack UI. Aspect ratio is constrained
    and the chosen size persists in plugin state.
*/
class AudioRackEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AudioRackEditor (AudioRackProcessor&);

    void resized() override;

private:
    AudioRackProcessor& rackProcessor;
    RackWebView webView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRackEditor)
};

} // namespace audiorack
