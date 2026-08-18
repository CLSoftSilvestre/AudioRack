#pragma once

#include "AudioModule.h"

#include <memory>
#include <string_view>
#include <vector>

namespace audiorack
{

/** Everything the host needs to know about a module type without an instance. */
struct ModuleTypeInfo
{
    ModuleDescriptor descriptor;
    std::unique_ptr<AudioModule> (*create)();
    void (*declareParameters) (ParameterBuilder&);
};

/** id -> factory table for all module types.

    Registration is a single explicit line per module in
    registerBuiltinModules() rather than self-registering static objects:
    static registrars get dead-stripped when modules live in static libraries,
    and an explicit list keeps registration order deterministic (parameter
    layout depends on it).

    Adding a module touches exactly: its folder in src/dsp/, one line in
    registerBuiltinModules(), and a faceplate in ui/src/units/.
*/
class ModuleRegistry
{
public:
    static ModuleRegistry& instance();

    void add (const ModuleTypeInfo& info);

    const ModuleTypeInfo* find (std::string_view moduleId) const noexcept;
    const std::vector<ModuleTypeInfo>& types() const noexcept { return typeList; }

    /// Convenience: create an instance by id, or nullptr for unknown ids.
    std::unique_ptr<AudioModule> createInstance (std::string_view moduleId) const;

private:
    ModuleRegistry() = default;

    std::vector<ModuleTypeInfo> typeList;
};

/// Registers every built-in module exactly once. Safe to call repeatedly.
void registerBuiltinModules();

} // namespace audiorack
