#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace audiorack
{

/** Declaration of one module parameter, independent of any APVTS instance.

    Modules declare these once, statically. M2 turns each spec into a
    juce::AudioParameterFloat with a namespaced ID (see makeParamID). Parameter
    IDs are part of the shipped state schema: never rename one — deprecate and
    add instead.
*/
struct ParamSpec
{
    juce::String                   idSuffix;   ///< e.g. "gaindb" — final ID is "slot3.gain.gaindb"
    juce::String                   displayName;
    juce::NormalisableRange<float> range;
    float                          defaultValue = 0.0f;
    juce::String                   unit;       ///< "dB", "ms", "Hz", "%", ""
    juce::StringArray              choices {}; ///< non-empty => discrete choice parameter;
                                               ///< range/defaultValue are then index-valued

    bool isChoice() const noexcept { return ! choices.isEmpty(); }
};

/// Collects ParamSpecs from a module's static declareParameters().
class ParameterBuilder
{
public:
    void add (ParamSpec spec) { specList.push_back (std::move (spec)); }

    const std::vector<ParamSpec>& specs() const noexcept { return specList; }

private:
    std::vector<ParamSpec> specList;
};

/// "slot" + index + "." + moduleId + "." + suffix, e.g. "slot3.comp.threshold".
inline juce::String makeParamID (int slotIndex, const char* moduleId, const juce::String& suffix)
{
    return "slot" + juce::String (slotIndex) + "." + moduleId + "." + suffix;
}

/// Per-slot host parameters that exist independently of the mounted module.
inline juce::String makeSlotParamID (int slotIndex, const juce::String& suffix)
{
    return "slot" + juce::String (slotIndex) + "." + suffix;
}

} // namespace audiorack
