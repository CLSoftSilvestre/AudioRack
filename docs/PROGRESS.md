# Progress

## 2026-08-18 (M8a) — crash-safe standalone state + performance pass

First slice of M8 (Ship). See ADR 0007.

### Done

- **Crash-safe standalone state** (`src/state/SessionRecovery.*`) — the
  standalone owns its own state (the plugin's is host-owned), and JUCE only
  persists it on a *clean* shutdown, so a crash lost the session. Now the
  processor autosaves the full state atomically (temp + rename, so a crash never
  leaves a half-written file) to `<appData>/AudioRack/session.autosave.json`
  every ~4 s on its existing 30 Hz timer, guarded by a `session.lock` sentinel.
  Finding the lock at the next launch means the previous run crashed → the
  recovered snapshot is applied on the first timer tick (after the standalone
  holder's own restore, so the fresher state wins). Standalone-only:
  `wrapperType == wrapperType_Standalone`; `recovery` is null in the plugin.
- **Performance pass** (`tests/RackBench.cpp`, target `audiorack_bench`) — times
  `RackEngine::process` on a full 12-slot rack (every module type cycled) over
  30 000 blocks against the realtime budget.

### Measured / verified

- **43 DSP/unit tests green** (was 37; +6 `SessionRecoveryTests`: fresh dir,
  write/dedupe, unclean-shutdown recovery, clean-shutdown no-recovery, atomic
  write leaves no temp, clearSnapshot).
- **`pluginval --strictness-level 10` passes** on the VST3 (recovery's null
  path), incl. state fuzz — zero warnings.
- **Crash-safe loop verified against the built app**: running app creates the
  lock + a 108 KB autosave; clean quit removes the lock; a planted stale lock +
  distinctive snapshot is adopted on relaunch over the holder's own state.
- **Benchmark (Release, 2019 Intel Mac):** full rack incl. 4×-oversampled
  saturator/limiter, FDN reverb, 6-band EQ, Lagrange delay —
  - 512 @ 48 kHz (budget 10.67 ms): p50 **5.5 %** / p95 7.9 % / p99 8.9 % / max 35.7 %.
  - 64 @ 96 kHz (budget 0.67 ms): p50 **10.8 %** / p95 16.3 % / p99 23.4 % / max 82.3 %.
  - Never crosses 100 % (no dropout); `max` outliers are OS scheduling jitter.

### Next (M8b / M8c)

Installers (macOS `.pkg` via pkgbuild/productbuild, Windows InnoSetup), code
signing + notarisation docs, WebView2 bootstrapper bundling (M8b); user manual
(M8c). Still open: host-automation hands-on pass in a real DAW.

## 2026-08-18 (later still ×3) — M7b MIDI learn

Completes M7 (Rack UX). See ADR 0006.

### Done

- **MIDI input enabled** — `NEEDS_MIDI_INPUT TRUE` + `acceptsMidi()`. VST3/AU now
  expose a MIDI input; the standalone accepts a MIDI device. Audio bus layout
  (incl. sidechain) unchanged.
- **Realtime-safe CC → parameter** — audio thread raw-parses control-change
  bytes (no `MidiMessage`, no allocation), coalesces the newest value per
  parameter into atomics; the 30 Hz message-thread timer applies them with
  `setValueNotifyingHost` (the only RT-unsafe call, kept off the audio thread).
  `ccToParam[128]` atomics are the map; the message thread owns all mutation.
- **Learn handshake** — arm a parameter (right-click → MIDI Learn); the next CC
  binds to it. One parameter ↔ at most one CC. Armed ring + "CCn" badge in the UI.
- **Wired for knobs & switches**; selectors excluded (right-click there already
  steps their value). Shared `contextMenu.ts` now backs both rack and control
  menus.
- **Persistence** — mappings saved as `"midi": [[cc, paramId], ...]`; load
  replaces the live map; armed state is transient.
- **Bridge** — outbound `ar_midi { armed, map }`; inbound `midiLearn` /
  `midiClearLearn` / `midiForget`. Browser mock binds a synthetic CC 1.2 s after
  arming for preview.

### Verified

- 37 DSP tests green (M7b adds no DSP).
- `pluginval --strictness-level 10` passes **with MIDI input enabled** —
  audio-processing (MIDI), Editor + Editor Automation, and param/state fuzz;
  zero warnings.
- Clean `tsc` + Vite build. Headless-preview screenshots confirm the armed
  amber ring and the resulting `CC20` badge (`?miditest` QA hook drives the real
  right-click → MIDI Learn path).

### Next (M8 — ship)

Installers (pkg / InnoSetup), code signing + notarisation docs, crash-safe
state, performance pass, user manual. Still open: host-automation hands-on pass
in a real DAW.

## 2026-08-18 (later still ×2) — M7a rack UX (drag & drop + A/B)

M7 is split into two commits; this is **M7a**. M7b (MIDI learn) is next.

### Done

- **Module browser** (`ui/src/rack/BrowserPanel.ts`) — left-hand palette of all
  registered modules, grouped by category with U-heights. Each chip is a drag
  source; double-click adds to the first free slot. The panel doubles as the
  "drop here to remove" zone.
- **Drag & drop** (`ui/src/rack/dnd.ts`) — pointer-based (not HTML5 DnD, which
  is unreliable in WKWebView/WebView2), with a follow-the-cursor ghost and
  green/red drop highlighting. Grab a mounted unit by its **rack ears** to
  reorder (swap) or drag it onto the browser to remove; drop a chip onto an
  empty slot to mount. Hit-testing via `elementsFromPoint` is scale-safe.
- **Duplicate** — right-click a unit → Duplicate copies it (values and all) into
  the first empty slot (`AudioRackProcessor::duplicateModule`).
- **A/B compare** — top toolbar with A / B / COPY. Two parameter snapshots
  (rack layout shared); selecting a bank recalls its values, COPY equalises them.
  Only the inactive bank is stored; the active one is the live APVTS state,
  synced on save. Persisted in state JSON under an optional `"ab"` node
  (schema stays v1; older presets seed both banks from their loaded values).
- **Layout** — studio is now `[ browser | scrolling stage ]`; the stage
  (toolbar + rack) scales to its column. Default editor grew to 1300×780.

### Measured / verified

- 37 DSP tests green (unchanged — M7a adds no DSP).
- `pluginval --strictness-level 10` passes incl. GUI: **Editor**, **Open editor
  whilst processing**, **Editor Automation**, and **Fuzz parameters/state**
  (the last round-trips the new A/B `"ab"` state) — zero warnings.
- Clean `tsc --noEmit` + Vite build (bundle app.js 50 kB / app.css 26 kB).
- Frame budget (`?full&bench`, 12-unit rack, headless Chrome): **p50 0.20 ms /
  p95 0.30 ms / p99 0.40 ms / max 0.50 ms** — unchanged, well under 4 ms.
- Browser-preview screenshots confirm the panel, toolbar (A lit), and scaled
  rack render correctly.

### Not done here (M7b — next)

MIDI learn: enable a MIDI input bus on the (currently MIDI-free) effect, a
realtime-safe CC→parameter map, learn-arm from the UI, and persisted mappings.
Also still open from earlier: host automation hands-on pass in a real DAW.

## 2026-08-18 (later still) — M6 time & tone

### Done

- **Parametric EQ (EQ-6, 2U)** — 6 RBJ biquad bands (bell/shelf/HP/LP),
  per-band on/solo, output trim, coefficients recomputed only on change. UI
  draws the live magnitude curve from the same RBJ formulas with draggable
  band handles.
- **Delay (DL-2, 2U)** — digital + tape modes, 4-point Lagrange fractional
  delay (glitch-free under modulation), host-sync note divisions, ping-pong,
  feedback tone filter, wow/flutter, stereo offset, tanh feedback clip.
- **Reverb (RV-8, 3U)** — 8-line FDN with lossless 8×8 Hadamard mixing,
  per-line damping, slow line modulation, predelay, width, freeze.
- **Saturator (SAT-1, 1U)** — tube/tape/transistor closed-form curves, 4×
  oversampled signal path, DC blocker, drive compensation, parallel mix.
- **Shared DSP**: Biquad gained peak/shelf coefficient sets, `FractionalDelay`
  (Lagrange), reused across EQ/delay/reverb.
- **UI**: EqCurve canvas widget, Selector (choice) widget, four themed
  faceplates (graphite EQ, bronze saturator, teal delay, indigo reverb),
  browser-preview parameter schema so `npm run dev` shows all modules.

### Measured (tests/TimeToneTests.cpp — 11 new tests, 37 total green)

- EQ +12 dB bell reads +12 ±1.5 at centre, flat two decades away; HP >10 dB
  down below cutoff; solo isolates bands.
- Delay impulse at exactly the set time; 1/4 @ 120 bpm = 24000 samples;
  feedback decays.
- Reverb tail present and decaying; freeze sustains and stays finite.
- Saturator generates odd harmonics; auto-gain holds level within 8 dB over
  21 dB of drive; reports oversampler latency.
- pluginval strictness 10 (incl. GUI tests) passes on the full 8-module
  layout; zero warnings.

### Next (M7 — rack UX)

Drag & drop from a browser panel, drag-to-reorder, A/B compare, per-slot
wet/dry already exists, MIDI learn, resizing polish.

## 2026-08-18 (later) — M5 core dynamics

### Done

- **ProcessContext** (ADR 0003): modules now receive transport + external
  sidechain bus pointers in one struct; processor forwards the host's
  sidechain bus when enabled.
- **Compressor (CMP-2, 2U)** — Giannoulis/Massberg/Reiss dB-domain design:
  FF/FB topologies, peak/RMS, quadratic soft knee, decoupled attack/release
  smoothing, program-dependent release (τ scaled 0.25–4× by 1 s GR average),
  RBJ sidechain HPF, external sidechain, auto-makeup (−G(0)/2), GR metering.
- **Limiter (LM-1, 1U)** — lookahead sliding-min + cascaded-boxcar gain chain
  (no overshoot by construction), 4× oversampled true-peak detection on the
  detector only, honest latency (lookahead + FIR delay) reported per block.
- **Gate (GT-1, 1U)** — hysteresis state machine, hold, range, SC HPF,
  external SC, audio-only lookahead delay reported as latency.
- **Choice parameters**: `ParamSpec.choices` → `AudioParameterChoice`; hosts
  now show "Feed-Forward/Feed-Back" etc. APVTS grew to 324 parameters
  (12 slots × 27); pluginval strictness 10 still passes, zero warnings.
- **UI**: three new faceplates with distinct hardware identities (charcoal
  CMP-2 with GR VU needle meter, black/red LM-1 with GR LED strip + TP LED,
  steel-blue GT-1 with OPEN status LED), GR mode for the VU meter widget,
  horizontal GrLadder widget, module picker on empty slots, 2U slot heights.

### Measured (tests/DynamicsTests.cpp — 15 new tests, 26 total green)

- Compressor static curve slope 1/R ±0.06; knee GR at threshold 1.125 dB
  ±0.6; attack 63% point inside [4, 30] ms for τ=10 ms; release 37% point
  inside [80, 500] ms for τ=200 ms.
- Limiter: impulse latency == `latencySamples()` exactly; null vs delayed
  input < 1e-4 at −12 dB; hostile +6 dB programme never exceeds ceiling
  ×1.0001.
- Gate: ≥50 dB attenuation closed; opens <10 ms; hold/release tail timed via
  −50 dB probe; no chatter inside hysteresis window; lookahead preserves
  ≥1.5× more transient.

### Next (M6 — time & tone)

EQ (6-band, live magnitude curve), Delay (digital+tape, host sync,
ping-pong, wow/flutter), Reverb (FDN or Dattorro), Saturator (oversampled
waveshaping), shared oversampling helper, transport sync plumbing (bpm/ppq
already in TransportInfo).

## 2026-08-18 — M0 through M4 (initial session)

### Done

- **M0** (`14a1997`): CMake 3.22 + JUCE 8.0.15 (FetchContent), presets for
  macOS/Windows, passthrough Standalone + VST3 + AU, external sidechain bus
  declared from day one, CI (build + tests + pluginval on both OSes), AGPLv3
  licence path documented. pluginval strictness 10 passes locally.
- **M1** (`38566eb`): RackEngine (serial chain, per-slot click-free
  bypass/wet-dry), JUCE-free SPSC queue, command/disposal queues (no
  audio-thread deletion), latency summing, ModuleRegistry, Gain module with
  docs. 11 Catch2 tests green.
- **M2** (`ac41f7f`): APVTS with `slotN.<module>.<param>` namespacing,
  mount/unmount/move with value-copy semantics, schema-versioned JSON state
  (v1) with migration hook, editor size persisted. pluginval fuzzing passes.
- **M3 + M4**: WebBrowserComponent host with resource provider (BinaryData +
  `AUDIORACK_UI_DIR` disk override), typed event bridge, meter SPSC ring
  (4096) folded at 60 Hz, param dirty-flag flush at 30 Hz, resizable
  aspect-locked editor. Vite/TS app: photoreal rack frame (posts, punched
  holes, blank panels), GN-1 Gain faceplate (knob, stereo LED ladder, VU with
  ANSI ballistics, bypass bat switch, power LED), widget library (Knob, Fader,
  VuMeter, LedLadder, Switch, Display, Screw) and `ui/dev/` gallery.

### Measured

- Frame budget (M3 DoD): full 12-unit rack, 600 simulated frames of meter
  ingestion + all animation ticks + forced style/layout flush, Chrome 2019-era
  Intel Mac: **p50 0.20 ms / p95 0.30 ms / p99 0.40 ms / max 0.70 ms** —
  comfortably inside the < 4 ms budget. (Compositor cost excluded; all meter
  motion is transform/class-only.)

### Environment caveats (this dev machine)

- CLT clang 12.0.5 + macOS 11.3 SDK: builds JUCE 8.0.15 fine, but its TSan
  runtime segfaults on macOS 15 → **TSan runs in CI** (`tsan` job), not
  locally. Update Command Line Tools to fix locally.
- No Screen Recording/Accessibility grants for the agent shell, so the
  standalone's window wasn't screenshot-verified; it launches and quits
  cleanly, pluginval GUI tests pass, and the identical bundle renders
  correctly in Chrome (screenshots reviewed).

### Next (M5 — core dynamics)

- Compressor (FF/FB, peak/RMS, soft knee dB-domain, program-dependent release,
  sidechain HPF + external SC, auto-makeup, GR meter), Limiter (true-peak,
  lookahead + latency report, 4x oversampled detection), Gate (hysteresis,
  hold, range, SC filter, lookahead). Static-curve + timing tests, null tests.
- Faceplates for all three; GR metering path already exists (MeterFrame.grDb).

### Uncertain / open

- Host automation "verified in a real DAW" (M2 DoD) still needs a manual pass
  in Live/Logic/Reaper — parameters are visible and fuzzable via pluginval,
  but nobody has dragged them in a DAW session yet.
- Windows build is CI-only so far; WebView2 runtime assumed present (ships
  with Win10/11; installer should bundle the Evergreen bootstrapper at M8).
- juceaide's BinaryData symbol naming is assumed stable for
  `index_html/app_js/app_css`; verified on macOS, double-check on Windows CI.
