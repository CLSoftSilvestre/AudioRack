#include "RackEngine.h"

namespace audiorack
{

RackEngine::~RackEngine()
{
    // Audio has stopped by the time the engine dies. Reclaim everything.
    collectGarbage();

    for (auto& s : slots)
    {
        delete s.module;
        s.module = nullptr;
    }
}

void RackEngine::prepare (double newSampleRate, int newMaxBlockSize, int newNumChannels)
{
    sampleRate   = newSampleRate;
    maxBlockSize = newMaxBlockSize;
    numChannels  = newNumChannels;

    // No processBlock can run during prepareToPlay, so consuming the command
    // queue here is safe and ensures modules in flight get prepared too.
    RackCommand cmd;
    while (commands.pop (cmd))
        applyCommand (cmd);
    collectGarbage();

    dryBuffer.setSize (numChannels, maxBlockSize, false, false, true);

    for (auto& s : slots)
    {
        s.prepare (sampleRate);

        if (s.module != nullptr)
            s.module->prepare (sampleRate, maxBlockSize, numChannels);
    }

    updateLatency();
}

bool RackEngine::setModule (int slotIndex, std::unique_ptr<AudioModule> module)
{
    jassert (juce::isPositiveAndBelow (slotIndex, kMaxSlots));
    jassert (module != nullptr);

    if (sampleRate > 0.0 && maxBlockSize > 0)
        module->prepare (sampleRate, maxBlockSize, numChannels);

    RackCommand cmd { RackCommand::Type::setModule, slotIndex, 0, module.get() };

    if (! commands.push (cmd))
        return false;                        // queue full; unique_ptr keeps ownership

    mirror[static_cast<size_t> (slotIndex)] = module.release();
    return true;
}

bool RackEngine::clearSlot (int slotIndex)
{
    jassert (juce::isPositiveAndBelow (slotIndex, kMaxSlots));

    if (! commands.push ({ RackCommand::Type::clearSlot, slotIndex, 0, nullptr }))
        return false;

    mirror[static_cast<size_t> (slotIndex)] = nullptr;
    return true;
}

bool RackEngine::moveModule (int fromSlot, int toSlot)
{
    jassert (juce::isPositiveAndBelow (fromSlot, kMaxSlots));
    jassert (juce::isPositiveAndBelow (toSlot, kMaxSlots));

    if (fromSlot == toSlot)
        return true;

    if (! commands.push ({ RackCommand::Type::moveModule, fromSlot, toSlot, nullptr }))
        return false;

    auto& from = mirror[static_cast<size_t> (fromSlot)];
    auto& to   = mirror[static_cast<size_t> (toSlot)];
    to   = from;
    from = nullptr;
    return true;
}

void RackEngine::collectGarbage()
{
    AudioModule* dead = nullptr;

    while (disposal.pop (dead))
        delete dead;
}

void RackEngine::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    RackCommand cmd;
    while (commands.pop (cmd))
        applyCommand (cmd);

    const auto numSamples  = static_cast<int> (block.getNumSamples());
    const auto blockChans  = static_cast<int> (block.getNumChannels());
    const auto mixChannels = juce::jmin (blockChans, dryBuffer.getNumChannels());

    for (int slotIndex = 0; slotIndex < kMaxSlots; ++slotIndex)
    {
        auto& s = slots[static_cast<size_t> (slotIndex)];
        s.wetGain.setTargetValue (s.targetWetGain());

        auto* module = s.module;

        if (module == nullptr)
        {
            s.wetGain.skip (numSamples);
            continue;
        }

        const bool  ramping = s.wetGain.isSmoothing();
        const float target  = s.wetGain.getTargetValue();

        if (! ramping && target <= 0.0f)
            continue;                        // fully bypassed: skip (module state freezes)

        const bool needsMix = ramping || target < 1.0f;

        if (needsMix)
            for (int ch = 0; ch < mixChannels; ++ch)
                dryBuffer.copyFrom (ch, 0, block.getChannelPointer (static_cast<size_t> (ch)), numSamples);

        module->process (block, context);

        {
            SlotMeterFrame meterFrame { slotIndex, {} };
            module->getMeterFrame (meterFrame.frame);
            meters.push (meterFrame);          // full queue: drop, UI folds what arrives
        }

        if (! needsMix)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            const float wet = s.wetGain.getNextValue();
            const float dry = 1.0f - wet;

            for (int ch = 0; ch < mixChannels; ++ch)
            {
                auto* out = block.getChannelPointer (static_cast<size_t> (ch));
                out[i] = out[i] * wet + dryBuffer.getSample (ch, i) * dry;
            }
        }
    }
}

void RackEngine::applyCommand (const RackCommand& cmd) noexcept
{
    auto& target = slots[static_cast<size_t> (cmd.slot)];

    switch (cmd.type)
    {
        case RackCommand::Type::setModule:
            discard (target.module);
            target.module = cmd.module;
            break;

        case RackCommand::Type::clearSlot:
            discard (target.module);
            target.module = nullptr;
            break;

        case RackCommand::Type::moveModule:
        {
            auto& dest = slots[static_cast<size_t> (cmd.otherSlot)];
            discard (dest.module);
            dest.module   = target.module;
            target.module = nullptr;
            break;
        }
    }

    updateLatency();
}

void RackEngine::discard (AudioModule* module) noexcept
{
    if (module == nullptr)
        return;

    // Sized so this cannot fail (disposal >> commands); never delete here.
    const bool pushed = disposal.push (module);
    jassertquiet (pushed);
}

void RackEngine::updateLatency() noexcept
{
    int total = 0;

    for (const auto& s : slots)
        if (s.module != nullptr)
            total += s.module->latencySamples();   // bypassed modules still count: no PDC jumps

    totalLatency.store (total, std::memory_order_relaxed);
}

} // namespace audiorack
