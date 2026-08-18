#include "RackWebView.h"

#include "../core/ModuleRegistry.h"
#include "UiAssets.h"

namespace audiorack
{

namespace
{
    const char* mimeForExtension (const juce::String& extension)
    {
        if (extension == "html") return "text/html";
        if (extension == "js")   return "text/javascript";
        if (extension == "css")  return "text/css";
        if (extension == "svg")  return "image/svg+xml";
        if (extension == "woff2") return "font/woff2";
        return "application/octet-stream";
    }

    std::vector<std::byte> toBytes (const void* data, int size)
    {
        const auto* raw = static_cast<const std::byte*> (data);
        return { raw, raw + size };
    }
} // namespace

static juce::String parameterDisplayText (const juce::RangedAudioParameter& p)
{
    const auto label = p.getLabel();
    return label.isEmpty() ? p.getCurrentValueAsText()
                           : p.getCurrentValueAsText() + " " + label;
}

RackWebView::RackWebView (AudioRackProcessor& p)
    : processor (p)
{
    auto& apvts = processor.parameterState();

    for (auto* parameter : processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            parameterIndex[ranged->paramID] = static_cast<int> (parameters.size());
            parameters.push_back (ranged);
        }
    }

    dirtyFlags = std::make_unique<std::atomic<bool>[]> (parameters.size());

    for (auto* ranged : parameters)
        apvts.addParameterListener (ranged->paramID, this);

    processor.rackLayoutChanged.addChangeListener (this);

    auto options =
        juce::WebBrowserComponent::Options {}
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withEventListener ("ar_ui", [this] (const juce::var& payload) { handleUiEvent (payload); })
            .withResourceProvider ([this] (const auto& url) { return serveResource (url); });

   #if JUCE_WINDOWS
    options = options
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (
            juce::WebBrowserComponent::Options::WinWebView2 {}
                .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)));
   #endif

    web = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*web);

    web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    startTimerHz (60);
}

RackWebView::~RackWebView()
{
    stopTimer();
    processor.rackLayoutChanged.removeChangeListener (this);

    for (auto* ranged : parameters)
        processor.parameterState().removeParameterListener (ranged->paramID, this);
}

void RackWebView::resized()
{
    web->setBounds (getLocalBounds());
}

// --- outbound -------------------------------------------------------------------

void RackWebView::sendRackLayout()
{
    auto* payload = new juce::DynamicObject();

    juce::Array<juce::var> slots;
    for (int slot = 0; slot < kMaxSlots; ++slot)
        slots.add (processor.mountedModuleId (slot));
    payload->setProperty ("slots", slots);

    juce::Array<juce::var> modules;
    for (const auto& type : ModuleRegistry::instance().types())
    {
        auto* m = new juce::DynamicObject();
        m->setProperty ("id",       type.descriptor.id);
        m->setProperty ("name",     type.descriptor.name);
        m->setProperty ("category", type.descriptor.category);
        m->setProperty ("units",    type.descriptor.rackUnits);
        modules.add (juce::var (m));
    }
    payload->setProperty ("modules", modules);

    web->emitEventIfBrowserIsVisible ("ar_rack", juce::var (payload));
}

void RackWebView::sendAllParameters()
{
    juce::Array<juce::var> batch;

    for (auto* ranged : parameters)
    {
        juce::Array<juce::var> entry;
        entry.add (ranged->paramID);
        entry.add (ranged->getValue());
        entry.add (parameterDisplayText (*ranged));
        batch.add (juce::var (entry));
    }

    auto* payload = new juce::DynamicObject();
    payload->setProperty ("p", batch);
    web->emitEventIfBrowserIsVisible ("ar_params", juce::var (payload));
}

void RackWebView::flushDirtyParameters()
{
    juce::Array<juce::var> batch;

    for (size_t i = 0; i < parameters.size(); ++i)
    {
        if (! dirtyFlags[i].exchange (false, std::memory_order_acq_rel))
            continue;

        auto* ranged = parameters[i];

        juce::Array<juce::var> entry;
        entry.add (ranged->paramID);
        entry.add (ranged->getValue());
        entry.add (parameterDisplayText (*ranged));
        batch.add (juce::var (entry));
    }

    if (batch.isEmpty())
        return;

    auto* payload = new juce::DynamicObject();
    payload->setProperty ("p", batch);
    web->emitEventIfBrowserIsVisible ("ar_params", juce::var (payload));
}

void RackWebView::pumpMeters()
{
    meterTouched.fill (false);

    SlotMeterFrame incoming;
    while (processor.rackEngine().popMeterFrame (incoming))
    {
        if (! juce::isPositiveAndBelow (incoming.slot, kMaxSlots))
            continue;

        auto& folded  = foldedMeters[static_cast<size_t> (incoming.slot)];
        auto& touched = meterTouched[static_cast<size_t> (incoming.slot)];

        if (! touched)
        {
            folded  = incoming.frame;   // first frame this tick: take it whole
            touched = true;
            continue;
        }

        // Fold: peaks must never be lost between polls, RMS takes the newest.
        folded.peakL = juce::jmax (folded.peakL, incoming.frame.peakL);
        folded.peakR = juce::jmax (folded.peakR, incoming.frame.peakR);
        folded.rmsL  = incoming.frame.rmsL;
        folded.rmsR  = incoming.frame.rmsR;
        folded.gainReductionDb = juce::jmax (folded.gainReductionDb,
                                             incoming.frame.gainReductionDb);
    }

    juce::Array<juce::var> frames;

    for (int slot = 0; slot < kMaxSlots; ++slot)
    {
        if (! meterTouched[static_cast<size_t> (slot)])
            continue;

        const auto& f = foldedMeters[static_cast<size_t> (slot)];

        juce::Array<juce::var> entry;
        entry.add (slot);
        entry.add (f.peakL);
        entry.add (f.peakR);
        entry.add (f.rmsL);
        entry.add (f.rmsR);
        entry.add (f.gainReductionDb);
        frames.add (juce::var (entry));
    }

    if (frames.isEmpty())
        return;

    auto* payload = new juce::DynamicObject();
    payload->setProperty ("m", frames);
    web->emitEventIfBrowserIsVisible ("ar_meters", juce::var (payload));
}

// --- inbound --------------------------------------------------------------------

void RackWebView::handleUiEvent (const juce::var& payload)
{
    const auto type = payload.getProperty ("type", juce::var()).toString();

    if (type == "ready")
    {
        sendRackLayout();
        sendAllParameters();
        return;
    }

    if (type == "setParam" || type == "beginGesture" || type == "endGesture")
    {
        const auto id = payload.getProperty ("id", juce::var()).toString();
        auto* parameter = processor.parameterState().getParameter (id);

        if (parameter == nullptr)
            return;

        if (type == "setParam")
            parameter->setValueNotifyingHost (
                juce::jlimit (0.0f, 1.0f,
                              static_cast<float> (static_cast<double> (payload.getProperty ("value01", 0.0)))));
        else if (type == "beginGesture")
            parameter->beginChangeGesture();
        else
            parameter->endChangeGesture();

        return;
    }

    if (type == "mount")
    {
        processor.mountModule (payload.getProperty ("slot", 0),
                               payload.getProperty ("moduleId", juce::var()).toString());
        return;
    }

    if (type == "unmount")
    {
        processor.unmountSlot (payload.getProperty ("slot", 0));
        return;
    }

    if (type == "move")
    {
        processor.moveModule (payload.getProperty ("from", 0),
                              payload.getProperty ("to", 0));
        return;
    }
}

// --- listeners --------------------------------------------------------------------

void RackWebView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    sendRackLayout();       // always message thread (ChangeBroadcaster is async)
}

void RackWebView::parameterChanged (const juce::String& parameterID, float)
{
    // Possibly the audio thread (host automation): flag only, flush on timer.
    const auto it = parameterIndex.find (parameterID);

    if (it != parameterIndex.end())
        dirtyFlags[static_cast<size_t> (it->second)].store (true, std::memory_order_release);
}

void RackWebView::timerCallback()
{
    pumpMeters();                 // 60 Hz

    if (++timerTick % 2 == 0)     // 30 Hz
        flushDirtyParameters();
}

// --- resources ----------------------------------------------------------------------

std::optional<juce::WebBrowserComponent::Resource> RackWebView::serveResource (const juce::String& url)
{
    // Live-reload development: point AUDIORACK_UI_DIR at ui/dist to serve from
    // disk instead of the compiled-in bundle.
    static const auto devDir = juce::SystemStats::getEnvironmentVariable ("AUDIORACK_UI_DIR", {});

    const auto path = url == "/" ? juce::String ("/index.html") : url;

    if (devDir.isNotEmpty())
    {
        const auto file = juce::File (devDir).getChildFile (path.trimCharactersAtStart ("/"));

        if (file.existsAsFile())
        {
            juce::MemoryBlock contents;
            file.loadFileAsData (contents);
            return juce::WebBrowserComponent::Resource {
                toBytes (contents.getData(), static_cast<int> (contents.getSize())),
                mimeForExtension (file.getFileExtension().trimCharactersAtStart ("."))
            };
        }
    }

    struct Entry { const char* url; const char* resource; };
    static constexpr Entry entries[] = {
        { "/index.html", "index_html" },
        { "/app.js",     "app_js" },
        { "/app.css",    "app_css" },
    };

    for (const auto& entry : entries)
    {
        if (path == entry.url)
        {
            int size = 0;
            if (const auto* data = uiassets::getNamedResource (entry.resource, size))
                return juce::WebBrowserComponent::Resource {
                    toBytes (data, size),
                    mimeForExtension (path.fromLastOccurrenceOf (".", false, false))
                };
        }
    }

    return std::nullopt;
}

} // namespace audiorack
