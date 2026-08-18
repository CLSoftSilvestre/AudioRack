#pragma once

#include "CoreTypes.h"
#include "ParameterModel.h"

#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace audiorack
{

/** Abstract base for every rack module.

    Threading contract:
      - prepare()/reset()/bindParameter() run on the message thread, never
        while process() is running for this instance (the engine only mounts
        fully prepared modules and unmounts before destruction).
      - process(), latencySamples() and getMeterFrame() must be realtime-safe:
        no allocation, locks, strings, logging or exceptions.

    Parameters are declared once, statically, by declareParameters(); each
    concrete module also implements it (found via ModuleRegistry, not virtual).
    At runtime the host processor binds each declared parameter to an atomic
    owned by the APVTS via bindParameter().
*/
class AudioModule
{
public:
    virtual ~AudioModule() = default;

    // Identity (static data, used by registry, presets and UI)
    virtual ModuleDescriptor descriptor() const = 0;

    // Lifecycle — message thread
    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() = 0;

    /// Bind a declared parameter (by idSuffix) to its APVTS atomic. The
    /// pointer outlives the module. Unknown suffixes must be ignored.
    virtual void bindParameter (const juce::String& idSuffix, std::atomic<float>* value) = 0;

    // Audio thread — must be realtime-safe
    virtual void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept = 0;

    virtual int  latencySamples() const noexcept { return 0; }
    virtual void getMeterFrame (MeterFrame&) const noexcept {}
};

} // namespace audiorack
