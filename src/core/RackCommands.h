#pragma once

namespace audiorack
{

class AudioModule;

/** Structural change shipped from the message thread to the audio thread
    through a lock-free SPSC queue. Trivially copyable by design: ownership of
    `module` transfers through the queue (message thread prepares and releases,
    audio thread mounts), and displaced modules travel back through a second
    queue for message-thread destruction. Nothing is ever deleted on the audio
    thread.
*/
struct RackCommand
{
    enum class Type
    {
        setModule,   ///< mount `module` in `slot`, displacing any current one
        clearSlot,   ///< unmount whatever is in `slot`
        moveModule   ///< move the module in `slot` to `otherSlot` (displacing its occupant)
    };

    Type         type      = Type::clearSlot;
    int          slot      = 0;
    int          otherSlot = 0;
    AudioModule* module    = nullptr;
};

} // namespace audiorack
