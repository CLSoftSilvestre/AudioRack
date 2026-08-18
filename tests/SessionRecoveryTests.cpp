#include <state/SessionRecovery.h>

#include <catch2/catch_test_macros.hpp>

using namespace audiorack;

namespace
{

/// A scratch directory under the OS temp folder, deleted on destruction.
struct ScratchDir
{
    ScratchDir()
        : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("audiorack_recovery_test_"
                                  + juce::String (juce::Random::getSystemRandom().nextInt64())))
    {
        dir.createDirectory();
    }

    ~ScratchDir() { dir.deleteRecursively(); }

    juce::File dir;
};

} // namespace

TEST_CASE ("Fresh directory reports no crash", "[recovery]")
{
    ScratchDir scratch;

    SessionRecovery recovery (scratch.dir);
    const auto start = recovery.beginSession();

    CHECK_FALSE (start.recovered);
    CHECK (start.snapshot.isEmpty());
    CHECK (recovery.lockFile().existsAsFile());   // this session is now covered
}

TEST_CASE ("writeSnapshot persists and dedupes identical writes", "[recovery]")
{
    ScratchDir scratch;

    SessionRecovery recovery (scratch.dir);
    recovery.beginSession();

    CHECK (recovery.writeSnapshot ("{\"a\":1}"));          // first write happens
    CHECK_FALSE (recovery.writeSnapshot ("{\"a\":1}"));    // identical => skipped
    CHECK (recovery.writeSnapshot ("{\"a\":2}"));          // changed => written

    CHECK (recovery.autosaveFile().loadFileAsString() == "{\"a\":2}");
}

TEST_CASE ("Unclean shutdown recovers the last snapshot", "[recovery]")
{
    ScratchDir scratch;

    // Session 1: writes state, then "crashes" — never calls endSessionCleanly.
    {
        SessionRecovery first (scratch.dir);
        first.beginSession();
        first.writeSnapshot ("{\"rack\":\"work-in-progress\"}");
    }

    // Session 2 on the same directory sees the leftover lock.
    SessionRecovery second (scratch.dir);
    const auto start = second.beginSession();

    CHECK (start.recovered);
    CHECK (start.snapshot == "{\"rack\":\"work-in-progress\"}");
}

TEST_CASE ("Clean shutdown does not trigger recovery", "[recovery]")
{
    ScratchDir scratch;

    {
        SessionRecovery first (scratch.dir);
        first.beginSession();
        first.writeSnapshot ("{\"rack\":\"saved\"}");
        first.endSessionCleanly();                // lock removed
    }

    SessionRecovery second (scratch.dir);
    const auto start = second.beginSession();

    CHECK_FALSE (start.recovered);
    CHECK (start.snapshot.isEmpty());
}

TEST_CASE ("An atomic write leaves no temporary behind", "[recovery]")
{
    ScratchDir scratch;

    SessionRecovery recovery (scratch.dir);
    recovery.beginSession();
    recovery.writeSnapshot ("{\"x\":1}");

    // Only the autosave and the lock should remain — no stray *.temp siblings.
    int jsonFiles = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (scratch.dir, false, "*",
                                                            juce::File::findFiles))
    {
        const auto name = entry.getFile().getFileName();
        CHECK_FALSE (name.contains ("temp"));
        if (name.endsWith (".json"))
            ++jsonFiles;
    }

    CHECK (jsonFiles == 1);
}

TEST_CASE ("clearSnapshot forgets stored recovery", "[recovery]")
{
    ScratchDir scratch;

    {
        SessionRecovery first (scratch.dir);
        first.beginSession();
        first.writeSnapshot ("{\"rack\":\"gone\"}");
        // crash (no clean shutdown), but then the snapshot is cleared:
        first.clearSnapshot();
    }

    SessionRecovery second (scratch.dir);
    const auto start = second.beginSession();

    CHECK_FALSE (start.recovered);   // lock present, but no snapshot to recover
}
