#include "RackState.h"

#include <map>

#include "../plugin/PluginProcessor.h"

namespace audiorack
{

juce::String rackStateToJson (const AudioRackProcessor& processor)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("schemaVersion", kStateSchemaVersion);

    juce::Array<juce::var> rack;
    for (int slot = 0; slot < kMaxSlots; ++slot)
        rack.add (processor.mountedModuleId (slot));
    root->setProperty ("rack", rack);

    auto* params = new juce::DynamicObject();
    for (const auto* parameter : processor.getParameters())
        if (const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*> (parameter))
            params->setProperty (ranged->paramID,
                                 ranged->convertFrom0to1 (ranged->getValue()));
    root->setProperty ("params", juce::var (params));

    // A/B banks (normalised 0..1 snapshots; the active bank was synced to live
    // by the caller). Additive and optional — older readers ignore it.
    auto bankToVar = [] (const std::map<juce::String, float>& snapshot)
    {
        auto* object = new juce::DynamicObject();
        for (const auto& [id, value] : snapshot)
            object->setProperty (id, static_cast<double> (value));
        return juce::var (object);
    };

    auto* ab = new juce::DynamicObject();
    ab->setProperty ("active", processor.activeBank());
    ab->setProperty ("a", bankToVar (processor.bankSnapshot (0)));
    ab->setProperty ("b", bankToVar (processor.bankSnapshot (1)));
    root->setProperty ("ab", juce::var (ab));

    // MIDI learn map, as [ [cc, paramId], ... ].
    juce::Array<juce::var> midi;
    for (const auto& [cc, id] : processor.midiMappings())
    {
        juce::Array<juce::var> entry;
        entry.add (cc);
        entry.add (id);
        midi.add (juce::var (entry));
    }
    root->setProperty ("midi", midi);

    auto* editor = new juce::DynamicObject();
    editor->setProperty ("width",  processor.editorSize().x);
    editor->setProperty ("height", processor.editorSize().y);
    root->setProperty ("editor", juce::var (editor));

    return juce::JSON::toString (juce::var (root));
}

bool applyRackStateJson (AudioRackProcessor& processor, const juce::String& json)
{
    const auto root = juce::JSON::parse (json);

    if (! root.isObject())
        return false;

    const int version = root.getProperty ("schemaVersion", 0);

    if (version < 1 || version > kStateSchemaVersion)
        return false;

    // Migrations from older schema versions chain here (none yet at v1).

    // 1. Rack layout: unmount everything, then mount what the preset names.
    if (const auto* rack = root.getProperty ("rack", juce::var()).getArray())
    {
        for (int slot = 0; slot < kMaxSlots; ++slot)
        {
            processor.unmountSlot (slot);

            if (slot < rack->size())
            {
                const auto moduleId = rack->getUnchecked (slot).toString();

                if (moduleId.isNotEmpty())
                    processor.mountModule (slot, moduleId);
            }
        }
    }

    // 2. Parameter values (plain, converted back through each param's range).
    if (auto* params = root.getProperty ("params", juce::var()).getDynamicObject())
    {
        for (const auto& entry : params->getProperties())
        {
            if (auto* parameter = processor.parameterState().getParameter (entry.name.toString()))
            {
                const float plain = static_cast<float> (static_cast<double> (entry.value));
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
            }
            // Unknown IDs are skipped silently: that's how deprecated
            // parameters from future-removed modules degrade gracefully.
        }
    }

    // 3. A/B banks. Absent in older presets: seed both from the loaded values.
    if (const auto ab = root.getProperty ("ab", juce::var()); ab.isObject())
    {
        auto readBank = [] (const juce::var& node)
        {
            std::map<juce::String, float> snapshot;
            if (auto* object = node.getDynamicObject())
                for (const auto& entry : object->getProperties())
                    snapshot[entry.name.toString()] =
                        static_cast<float> (static_cast<double> (entry.value));
            return snapshot;
        };

        processor.loadBankSnapshot (0, readBank (ab.getProperty ("a", juce::var())));
        processor.loadBankSnapshot (1, readBank (ab.getProperty ("b", juce::var())));
        processor.setActiveBank (static_cast<int> (ab.getProperty ("active", 0)));
    }
    else
    {
        processor.captureBank (0);
        processor.captureBank (1);
        processor.setActiveBank (0);
    }

    processor.abStateChanged.sendChangeMessage();

    // 4. MIDI learn map (replace whatever was there; absent = no mappings).
    processor.clearAllMidiMappings();
    if (const auto* midi = root.getProperty ("midi", juce::var()).getArray())
        for (const auto& entry : *midi)
            if (const auto* pair = entry.getArray(); pair != nullptr && pair->size() == 2)
                processor.setMidiMapping (static_cast<int> (pair->getUnchecked (0)),
                                          pair->getUnchecked (1).toString());
    processor.midiStateChanged.sendChangeMessage();

    // 5. Editor size.
    const auto editor = root.getProperty ("editor", juce::var());
    if (editor.isObject())
        processor.setEditorSize ({ editor.getProperty ("width",  1200),
                                   editor.getProperty ("height", 760) });

    return true;
}

} // namespace audiorack
