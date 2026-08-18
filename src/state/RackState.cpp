#include "RackState.h"

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

    // 3. Editor size.
    const auto editor = root.getProperty ("editor", juce::var());
    if (editor.isObject())
        processor.setEditorSize ({ editor.getProperty ("width",  1100),
                                   editor.getProperty ("height", 740) });

    return true;
}

} // namespace audiorack
