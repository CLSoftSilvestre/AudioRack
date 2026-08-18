# ADR 0006 — MIDI learn (M7b)

Date: 2026-08-18 · Status: accepted

M7b completes M7 (Rack UX). It enables a MIDI input on the plugin and lets any
continuous/binary control be driven by a MIDI CC via right-click → MIDI Learn.

## 1. Enabling MIDI input

The effect was MIDI-free. `NEEDS_MIDI_INPUT TRUE` (CMake) + `acceptsMidi()` now
expose a MIDI input bus on VST3/AU, and the standalone gains MIDI-device input.
No MIDI output, still not an `IS_MIDI_EFFECT`. The audio bus layout is unchanged,
so this does not disturb the sidechain work or `isBusesLayoutSupported`.

## 2. Realtime-safe CC → parameter path (atomics, no locks, no allocation)

The audio thread must not call `setValueNotifyingHost` (it notifies listeners
and mutates the APVTS value tree — may allocate/lock), and must not allocate.
So the map and the apply are split across threads:

- **Map:** `std::array<std::atomic<int>, 128> ccToParam` (CC → parameter index,
  −1 = unmapped). The audio thread only *reads* it; the message thread owns all
  mutation. Parameter index space = position in `automatableParams` (built once
  in the constructor, never mutated → safe to hold by index).
- **Audio thread** (`handleControlChange`, called from a raw-byte parse of the
  `MidiBuffer` — status `0xB0`, no `MidiMessage` construction, zero allocation):
  for a mapped CC it stores the newest value into a per-parameter atomic and
  sets a dirty flag. This **coalesces** a fast CC sweep to one value per param.
- **Message thread** (`drainMidi`, on the existing 30 Hz processor timer):
  applies each dirty parameter with `setValueNotifyingHost`. ~33 ms control
  latency — fine for hardware-knob control, and the only correct place to touch
  the parameter object.

Nothing here uses a queue: a single armed flag plus the coalescing atomics are
sufficient because there is at most one armed learn at a time and we only care
about the newest value per parameter. (`SpscQueue` remains for structural rack
commands, where ordering and per-event delivery matter.)

## 3. Learn handshake

Arming stores the target parameter index in `learnArmed` (audio-visible) and in
`messageArmedIndex` (message-thread authority). On the next CC the audio thread
publishes the CC number in `pendingLearnedCc` and disarms `learnArmed`; the timer
sees it, binds `ccToParam[cc] = messageArmedIndex`, and broadcasts. The audio
thread never mutates the map. One parameter is bound to at most one CC (binding
releases any previous CC for that parameter).

## 4. UI: right-click on knobs and switches

MIDI learn is offered from the shared control context menu (`contextMenu.ts`,
now also used by the rack menus). It is wired for **knobs and switches** only:
**selectors already use right-click to step their value**, so hijacking it there
would remove an existing interaction — discrete choice params are intentionally
excluded for now. An armed control shows a pulsing amber ring; a mapped control
shows a small "CCn" badge. State flows over a new `ar_midi` event
(`{ armed, map }`); the browser mock binds a synthetic CC 1.2 s after arming so
`npm run dev` demonstrates the flow without hardware.

## 5. Persistence

Mappings are stored in the state JSON as `"midi": [[cc, paramId], ...]` (paramID,
not index, so they survive across versions). Loading replaces the live map;
presets without the node clear it. The armed state is transient and not saved.

## 6. Verification

No new Catch2 test: the path is APVTS/plugin-config-bound (not in the
`juce_dsp`-only test target) and the RT-critical half is atomic bookkeeping, not
a DSP claim. Verified by: clean `tsc` + Vite build; `pluginval --strictness-level
10` (now with MIDI input enabled — audio-processing + editor-automation +
state fuzz all pass); and headless browser-preview screenshots of the armed ring
and the resulting `CCn` badge via the `?miditest` QA hook. The 37 DSP tests stay
green.
