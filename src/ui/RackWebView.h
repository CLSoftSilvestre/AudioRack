#pragma once

#include "../plugin/PluginProcessor.h"

#include <map>

namespace audiorack
{

/** Hosts the bundled TypeScript SPA in a juce::WebBrowserComponent and owns
    the whole native<->web bridge.

    Protocol (typed mirror lives in ui/src/bridge/protocol.ts):

      web -> native   event "ar_ui"
        { type: "ready" }
        { type: "setParam",     id, value01 }
        { type: "beginGesture", id } / { type: "endGesture", id }
        { type: "mount", slot, moduleId } / { type: "unmount", slot }
        { type: "move",  from, to }

      native -> web
        "ar_params" { p: [[id, value01, displayText], ...] }
        "ar_rack"   { slots: [moduleIdOrEmpty x kMaxSlots],
                      modules: [{id, name, category, units}, ...] }
        "ar_meters" { m: [[slot, peakL, peakR, rmsL, rmsR, grDb], ...] }

    Threading: parameter-change callbacks can arrive on the audio thread, so
    they only flip an atomic dirty flag; a 60 Hz timer folds the meter ring
    buffer into one event per frame and flushes dirty parameters at 30 Hz.
*/
class RackWebView final : public juce::Component,
                          private juce::Timer,
                          private juce::ChangeListener,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit RackWebView (AudioRackProcessor&);
    ~RackWebView() override;

    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    void handleUiEvent (const juce::var& payload);
    void sendRackLayout();
    void sendAllParameters();
    void flushDirtyParameters();
    void pumpMeters();

    std::optional<juce::WebBrowserComponent::Resource> serveResource (const juce::String& url);

    AudioRackProcessor& processor;

    std::vector<juce::RangedAudioParameter*> parameters;       // stable order
    std::map<juce::String, int>              parameterIndex;   // id -> index
    std::unique_ptr<std::atomic<bool>[]>     dirtyFlags;

    std::array<MeterFrame, kMaxSlots> foldedMeters {};
    std::array<bool, kMaxSlots>       meterTouched {};

    std::unique_ptr<juce::WebBrowserComponent> web;

    int timerTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackWebView)
};

} // namespace audiorack
