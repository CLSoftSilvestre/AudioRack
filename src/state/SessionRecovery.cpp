#include "SessionRecovery.h"

namespace audiorack
{

juce::File SessionRecovery::defaultDirectory()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    // userApplicationDataDirectory is ~/Library on macOS; the conventional home
    // for app data is the "Application Support" child (where the standalone's
    // own .settings file also lives).
    base = base.getChildFile ("Application Support");
   #endif
    return base.getChildFile ("AudioRack");
}

SessionRecovery::SessionRecovery() : SessionRecovery (defaultDirectory()) {}

SessionRecovery::SessionRecovery (juce::File directory) : dir (std::move (directory))
{
    dir.createDirectory();
}

SessionRecovery::StartResult SessionRecovery::beginSession()
{
    StartResult result;

    if (lockFile().existsAsFile())
    {
        // The previous run left its lock behind => it did not shut down cleanly.
        const auto snap = autosaveFile();
        if (snap.existsAsFile())
        {
            result.snapshot  = snap.loadFileAsString();
            result.recovered = result.snapshot.isNotEmpty();
        }
    }

    // Seed the dedupe cache from whatever is already on disk so an unchanged
    // first autosave is skipped.
    lastWritten = autosaveFile().existsAsFile() ? autosaveFile().loadFileAsString()
                                                : juce::String();

    // (Re)assert the lock for this session.
    lockFile().replaceWithText (juce::String (juce::Time::getCurrentTime().toMilliseconds()));

    return result;
}

bool SessionRecovery::writeSnapshot (const juce::String& json)
{
    if (json == lastWritten)
        return false;

    // Write to a sibling temporary, then atomically swap it into place; a crash
    // mid-write leaves the previous complete file intact.
    juce::TemporaryFile temp (autosaveFile());

    if (! temp.getFile().replaceWithText (json))
        return false;

    if (! temp.overwriteTargetFileWithTemporary())
        return false;

    lastWritten = json;
    return true;
}

void SessionRecovery::endSessionCleanly()
{
    lockFile().deleteFile();
}

void SessionRecovery::clearSnapshot()
{
    autosaveFile().deleteFile();
    lastWritten = {};
}

} // namespace audiorack
