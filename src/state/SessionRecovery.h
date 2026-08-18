#pragma once

#include <juce_core/juce_core.h>

namespace audiorack
{

/** Crash-safe session persistence for the standalone application.

    The standalone owns its own state (unlike the plugin, whose state the host
    saves and restores). JUCE's StandalonePluginHolder only writes that state on
    a *clean* shutdown, so a crash loses everything since launch. This class
    closes that gap with two files in a per-user directory:

      - session.autosave.json : the latest full state snapshot, written
        atomically (temporary sibling + rename) so a crash can never leave it
        half-written — a reader always sees either the whole previous file or
        the whole new one.
      - session.lock          : present while a session is running, removed on
        clean shutdown. Finding it at the next launch means the previous run
        did not exit cleanly, i.e. it crashed (or was force-quit / lost power).

    All methods run on the message thread. Nothing here touches the audio
    thread — the processor serialises its state on its existing 30 Hz timer.
*/
class SessionRecovery
{
public:
    /// Uses the platform per-user data directory: <appData>/AudioRack.
    SessionRecovery();

    /// Explicit directory (used by tests). Created if it does not exist.
    explicit SessionRecovery (juce::File directory);

    struct StartResult
    {
        bool         recovered = false;   ///< a previous run crashed AND left a usable snapshot
        juce::String snapshot;            ///< the recovered JSON (empty unless recovered)
    };

    /** Marks a session as running and reports whether the previous one crashed.

        If a lock from an earlier run is found, that run did not shut down
        cleanly: returns recovered=true with the last autosaved snapshot (when
        one exists and is non-empty). Always (re)creates the lock so this run is
        itself covered.
    */
    StartResult beginSession();

    /** Atomically replaces the autosave with `json`.

        No-op returning false if `json` is identical to the last thing written
        (so the periodic autosaver does not churn the disk while the user is
        idle). Returns true when it actually wrote.
    */
    bool writeSnapshot (const juce::String& json);

    /// Removes the lock (clean shutdown). The autosave is left in place — it is
    /// harmless and simply overwritten next run.
    void endSessionCleanly();

    /// Forgets any stored snapshot (e.g. once a recovery has been consumed).
    void clearSnapshot();

    juce::File directory()    const noexcept { return dir; }
    juce::File autosaveFile() const noexcept { return dir.getChildFile ("session.autosave.json"); }
    juce::File lockFile()     const noexcept { return dir.getChildFile ("session.lock"); }

private:
    static juce::File defaultDirectory();

    juce::File   dir;
    juce::String lastWritten;   ///< dedupe cache for writeSnapshot

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionRecovery)
};

} // namespace audiorack
