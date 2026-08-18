# ADR 0002 — WebView bridge, parameter layout, rack-move semantics

Date: 2026-08-18 · Status: accepted

## 1. Bridge: typed event protocol instead of WebSliderRelay

The brief suggested JUCE 8's `WebSliderRelay` per parameter. We instead use
the same underlying transport (native-integration events) with one typed
protocol: a single `ar_ui` event inbound and `ar_params` / `ar_rack` /
`ar_meters` outbound, mirrored 1:1 by TypeScript types in
`ui/src/bridge/protocol.ts`.

Why: relays would mean one native object + one JS binding per parameter
(dozens now, ~1000 once all modules ship), pull in the `juce-framework-frontend`
npm dependency, and still not cover rack-structure messages or meter batches —
so a custom channel would exist anyway. One protocol file per side keeps the
contract reviewable in a single diff. The "typed, no ad-hoc strings" intent of
the brief is honoured by the shared type definitions, not by the relay class.

Consequences: parameter changes echo back through a 30 Hz batch flush
(audio-thread listener just flips an atomic dirty flag); meters fold through a
4096-deep SPSC ring drained at 60 Hz, taking max-of-peaks so dropped UI frames
never lose a peak.

## 2. Parameters: full slot x module-type pre-declaration

APVTS layouts are fixed at construction, so every slot declares parameters for
every registered module type (`slotN.<module>.<param>`), dormant until a
module is mounted. With the v1 module set this lands around ~1100 parameters
(12 slots x ~90); hosts routinely handle this (samplers expose more), and it
buys stable IDs, correct automation naming, and zero layout changes across
presets. If it ever hurts, the fallback is a per-slot generic parameter pool —
a schema migration, hence recorded here.

## 3. Moving a module = copy values + remount, not rebind

A mounted module's parameter bindings are immutable (rebinding pointers while
the audio thread reads them is a race). Reordering copies the source slot's
parameter values to the destination slot's parameters and mounts a fresh
instance there. DSP runtime state (delay tails, reverb energy) resets on
reorder; that is audible anyway, since the processing order changed.

## 4. UI bundle: deterministic names, embedded at configure time

Vite emits exactly `index.html` / `app.js` / `app.css` (no content hashes), so
CMake can embed them as BinaryData with stable symbol names. The bundle builds
during CMake configure; `AUDIORACK_UI_DIR=<path>` at runtime serves from disk
for live UI development instead.
