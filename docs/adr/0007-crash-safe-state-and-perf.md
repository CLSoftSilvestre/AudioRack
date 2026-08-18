# ADR 0007 — Crash-safe standalone state & the performance pass (M8a)

Date: 2026-08-18 · Status: accepted

M8 (Ship) is split like M7. This is **M8a**: crash-safe session state for the
standalone, plus a measured audio-thread performance pass. Installers +
signing/notarisation docs (M8b) and the user manual (M8c) follow.

## 1. Why the standalone needs its own crash safety

Plugin state is owned by the host: the DAW calls `getStateInformation` /
`setStateInformation` and persists the result in its project. The **standalone**
has no such host. JUCE's `StandalonePluginHolder` does persist the plugin state,
but only on a *clean* shutdown (in its destructor / on close). A crash, force
quit, or power loss therefore discards everything done since launch.

So crash safety is a **standalone-only** concern. Everywhere else `recovery` is
null and this code does nothing — verified by pluginval (VST3) still passing
strictness 10 with state fuzzing, exercising exactly the null path.

The processor distinguishes the two at construction with
`wrapperType == wrapperType_Standalone` (JUCE sets `wrapperType` before the
constructor body runs), and only then constructs a `SessionRecovery`.

## 2. The mechanism: an atomic autosave + a lock sentinel

`SessionRecovery` (in `src/state/`, depends only on `juce_core`) owns two files
in `<appData>/AudioRack/`:

- **`session.autosave.json`** — the latest full state snapshot (the same JSON
  `getStateInformation` produces). Written **atomically**: a `juce::TemporaryFile`
  sibling is written and then `overwriteTargetFileWithTemporary()` renames it
  into place. A crash mid-write can only leave the *previous* complete file — a
  reader never sees a half-written snapshot. This is the crux of "crash-safe".
- **`session.lock`** — created on `beginSession()`, deleted on
  `endSessionCleanly()`. Finding it at the next launch means the previous run
  never reached its clean shutdown, i.e. it crashed.

`writeSnapshot` dedupes against the last thing it wrote, so the periodic
autosaver does not churn the disk while the user is idle.

## 3. Wiring into the processor

- **Constructor** (standalone only): `beginSession()`. If it reports a crash
  *and* a snapshot exists, the JSON is stashed in `pendingRecovery`.
- **First timer tick**: `pendingRecovery` is applied via `applyRackStateJson`.
  This is deliberately deferred rather than done in the constructor, because the
  `StandalonePluginHolder` restores *its* (older, last-clean-exit) state via
  `setStateInformation` **after** the constructor returns. Applying on the first
  30 Hz tick — which only fires once the message loop is running, i.e. after
  startup — lets the fresher autosave win. `applyRackStateJson` fires the same
  UI broadcasters as a preset load, so an open editor refreshes.
- **Timer, every ~120 ticks (~4 s)**: `captureBank(currentBank)` then
  `writeSnapshot`. Cheap (a few KB of JSON on the message thread), deduped, and
  entirely off the audio thread.
- **Destructor**: a final `writeSnapshot`, then `endSessionCleanly()` drops the
  lock so the next launch is not mistaken for a crash.

Because the crash flag is only set when the lock is present, a normal clean run
never triggers a recovery override: the previous clean exit removed the lock.

### Verification

`SessionRecoveryTests.cpp` (6 Catch2 cases) covers the pure logic: fresh dir →
no crash; write/dedupe; unclean shutdown → recovers the last snapshot; clean
shutdown → no recovery; atomic write leaves no stray temp; `clearSnapshot`. The
integration was verified against the built app: while running it creates the
lock + a 108 KB autosave; a clean quit removes the lock; and after planting a
distinctive snapshot + a stale lock, the relaunched app adopted the recovered
rack over the holder's own restored state.

## 4. Performance pass

`tests/RackBench.cpp` (target `audiorack_bench`, **not** a ctest — absolute
timings are machine-dependent) fills all `kMaxSlots` (12) slots by cycling every
registered module type, binds each parameter to an atomic at its default, then
times `RackEngine::process` over 30 000 blocks after a warm-up, reporting the
per-block distribution against the realtime budget (`blockSize / sampleRate`).

Measured on this dev machine (2019 Intel Mac, Release):

| Config | mean | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| 512 @ 48 kHz (budget 10.67 ms) | 5.9 % | 5.5 % | 7.9 % | 8.9 % | 35.7 % |
| 64 @ 96 kHz (budget 0.67 ms)   | 11.5 %| 10.8 %| 16.3 %| 23.4 %| 82.3 % |

A fully loaded rack — including the 4×-oversampled saturator and limiter, the
8-line FDN reverb, a 6-band EQ and the Lagrange delay — stays well under one
core's realtime budget even at the punishing 64/96k config; the `max` outliers
are OS scheduling jitter and never cross 100 % (no dropout). This is the number
behind the M8 "performance pass" DoD item.
