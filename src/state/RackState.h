#pragma once

#include <juce_core/juce_core.h>

namespace audiorack
{

class AudioRackProcessor;

/// Current serialisation schema. Bump on any breaking change and add a
/// migration step in applyRackStateJson().
inline constexpr int kStateSchemaVersion = 1;

/** Serialises the full plugin state (rack layout, all parameter values,
    editor size) to JSON:

    {
      "schemaVersion": 1,
      "rack":   ["gain", "", "", ...],          // module id per slot, "" = empty
      "params": { "slot0.gain.gaindb": 0.0, "slot0.bypass": 0.0, ... },  // plain values
      "editor": { "width": 1100, "height": 740 }
    }
*/
juce::String rackStateToJson (const AudioRackProcessor&);

/** Applies a JSON state produced by rackStateToJson (any past schema version).
    Returns false if the payload is unusable; the processor is left in a valid
    (possibly default) state either way. Message thread only.
*/
bool applyRackStateJson (AudioRackProcessor&, const juce::String& json);

} // namespace audiorack
