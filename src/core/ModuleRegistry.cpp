#include "ModuleRegistry.h"

#include "../dsp/gain/GainModule.h"

namespace audiorack
{

ModuleRegistry& ModuleRegistry::instance()
{
    static ModuleRegistry registry;
    return registry;
}

void ModuleRegistry::add (const ModuleTypeInfo& info)
{
    jassert (find (info.descriptor.id) == nullptr);   // duplicate module id
    typeList.push_back (info);
}

const ModuleTypeInfo* ModuleRegistry::find (std::string_view moduleId) const noexcept
{
    for (const auto& info : typeList)
        if (moduleId == info.descriptor.id)
            return &info;

    return nullptr;
}

std::unique_ptr<AudioModule> ModuleRegistry::createInstance (std::string_view moduleId) const
{
    const auto* info = find (moduleId);
    return info != nullptr ? info->create() : nullptr;
}

void registerBuiltinModules()
{
    auto& registry = ModuleRegistry::instance();

    if (! registry.types().empty())
        return;

    registry.add (GainModule::typeInfo());
    // One line per module lands here as milestones M5/M6 add them.
}

} // namespace audiorack
