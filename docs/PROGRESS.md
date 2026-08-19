# Progress

## 2026-08-19 — Real-time analyser (shared) + EQ-6 RTA

EQ-6 now draws a live spectrum of its own output behind the response curve. The
analyser is built as a **shared component**, not EQ-private code, so the §5
Meter/Analyzer module becomes a faceplate over DSP that already exists and is
already tested. See `docs/adr/0009-realtime-analyser.md`.

### Design decisions (confirmed with the user)

- **Shared analyser + EQ overlay**, rather than an EQ-only FFT or building the
  full Meter/Analyzer module first. The expensive part was never the faceplate;
  it was the analyser DSP plus a transport for ~96 bands per module, since
  `MeterFrame` is five floats and cannot carry a spectrum.
- **Post-EQ tap.** The RTA shows what the EQ is putting out, so the curve and
  the spectrum describe the same signal. A PRE/POST switch was considered and
  deferred — it needs a parameter and a second analyser instance.

### Done

- `src/dsp/common/SpectrumAnalyser.h` — 4096-point Hann periodogram, 96
  geometric bands (20 Hz–22 kHz), fast-attack/slow-release ballistics,
  calibrated so a full-scale sine reads 0 dBFS. Audio thread only mono-sums
  into a lock-free ring; the FFT runs on the message thread.
- `SpectrumFrame` in `core/CoreTypes.h`; optional `AudioModule::readSpectrum()`
  (message thread) alongside the realtime `getMeterFrame()`.
- `EqModule` taps its own output into the analyser and implements
  `readSpectrum()`.
- `RackWebView::pumpSpectra()` — new `ar_spectrum` event at 30 Hz, on the timer
  tick opposite the parameter flush, bands sent as rounded half-dB integers.
- UI: `SpectrumMessage` + band-layout constants in `protocol.ts`,
  `Bridge.onSpectrum`, `Store.onSpectrum(slot)` with a reused `Float32Array`
  per slot, and `EqCurve` drawing the RTA under a cached grid/curve layer.
- `EqCurve` moved off its own `requestAnimationFrame` onto the shared
  `animator`, which is both the documented house rule and what makes the widget
  visible to the frame-budget bench.
- Mock bridge synthesises a pink-tilted spectrum with wandering resonances so
  `npm run dev` develops the widget against realistic motion; new `?eqfull`
  layout mounts twelve EQs for the analyser's worst case.
- `docs/modules/eq.md` updated; ADR 0009 written.

### Verified

- `tests/SpectrumTests.cpp` (8 cases): 0 dBFS calibration, level-tracks-dB,
  two-tone band separation, decay-not-freeze when the transport stops, silence
  at the floor, no spectrum before `prepare()`, the default `AudioModule` opting
  out, and — the point of the exercise — a −18 dB notch showing up in EQ-6's
  spectrum, which proves the tap is post-filter. Full suite: **35 075 assertions
  across 57 cases pass.**
- Clean build (warnings-as-errors) of Standalone + VST3 + AU on macOS.
- Native cost, measured by `audiorack_bench` on this machine (Release, 512
  samples @ 48 kHz): `push()` on the audio thread **0.48 µs p50 / 0.84 µs p95**
  = 0.01 % of the block budget; `render()` on the message thread **21.9 µs p50 /
  37.1 µs p95**, which at twelve slots and 30 Hz is **1.33 % of one core**.
- UI frame budget, measured over CDP with real timing (the existing
  `?full&bench` path reports zeros under headless Chrome's virtual time, so
  `runBench()` is now exposed as `window.__arBench()` for a driver to call):
  baseline `?full` p50 0.10 ms / p95 0.20 ms / p99 0.70 ms; `?eqfull` — twelve
  live RTAs — p50 **0.40 ms** / p95 **0.70 ms** / p99 **1.20 ms**, against the
  4 ms budget. The `?eqfull` figure is conservative: the mock bridge's spectrum
  synthesis (96 bands x 12 slots of `pow`/`log2`/`random` per tick) costs more
  than the native path, which just halves integers.

### Uncertain / future

- The low bands are coarser than they look: below ~450 Hz at 48 kHz each band is
  narrower than one FFT bin, so several adjacent bands interpolate the same pair
  of bins. Correct, but a larger FFT or a multi-resolution transform would give
  real detail down there.
- Release ballistics are a fixed per-call coefficient tuned for a 30 Hz render.
  If the editor's poll rate ever changes, the fall time changes with it; a
  time-based coefficient would decouple them.
- No RTA on/off control. Adding one means a host-automatable parameter, which
  is a heavy way to persist a display preference — worth revisiting alongside
  the Meter/Analyzer module.

## 2026-08-19 — Guitar Amp module (AMP-1)

New rack module: a guitar amplifier with cascaded tube preamp, passive-style
tone stack, power-amp saturation and switchable analytic speaker cabinets
(1x12 / 2x12 / 4x12). Ninth module; adds a new "Amp" category. See
`docs/modules/amp.md`.

### Design decisions (confirmed with the user)

- **Cabinets = analytic filter model**, not impulse-response files. Each cab is a
  chain of biquads (sub-resonance HP, box resonance, low-mid scoop, cone-breakup
  peak, top-end roll-off) plus a short mic-position comb. Fully original, no
  external assets, legally clean, tiny bundle, click-free — chosen over
  synthesized-IR convolution and (rejected) bundling copyrighted real IRs.
- **Three channels** — Clean / Crunch / Lead — selecting how many cascaded
  preamp stages the signal runs through, over a shared Bass/Mid/Treble/Presence
  tone stack driven by Gain and Master.

### Done

- `src/dsp/amp/AmpModule.{h,cpp}` — 4× oversampled preamp + tone stack + power
  amp (nonlinear stages in the oversampled domain, tone stack between preamp and
  power amp as in the real circuit), base-rate cabinet chain, oversampler latency
  reported. Registered in `ModuleRegistry` + `CMakeLists` DSP sources.
- Faceplate `ui/src/units/AmpUnit.ts` (3U): black-tolex face, gold Fender-style
  control plate (Gain/Bass/Middle/Treble/Presence/Master), Clean/Crunch/Lead
  channel tab, cabinet selector with a **live speaker diagram** (1/2/4 cones),
  grille-cloth-backed output VU, illuminated POWER rocker. Wired into
  `RackFrame`, the module browser (`juce.ts`), `previewSchema`, and the `?demo`
  rack.
- `docs/modules/amp.md`.

### Verified

- `tests/AmpTests.cpp` (5 cases, all green): preamp harmonic generation rises
  Clean < Crunch < Lead; treble/bass controls move their bands; each cab
  band-limits the top (1x12 > 2x12 > 4x12 HF energy); output finite + bounded at
  extremes; latency > 0. Full suite: **34 954 assertions across 49 cases pass.**
- Clean build (warnings-as-errors) of Standalone + VST3 + AU on macOS.
- Faceplate reviewed via headless `?demo` screenshots at multiple widths (fits
  the rack row, POWER rocker not clipped).

### Uncertain / future

- Tone stack is a shelf/peak approximation; a full interacting WDF network
  (Yeh & Smith 2006) would add the inherent mid scoop.
- Switching cab type re-derives filter coefficients at block rate without a
  crossfade; a discrete "swap" is expected to be an audible change, but a short
  crossfade could be added if clicks are reported.

## 2026-08-18 (M8c) — user manual · M8 complete

Final slice of M8 (Ship). **All milestones M0–M8 are now done.**

### Done

- **User manual** (`docs/MANUAL.md`) — end-user documentation: installing on
  macOS/Windows, first launch + standalone audio/mic setup, the rack UI tour,
  building a rack (drag & drop, reorder, duplicate, remove), control interactions
  (knob drag/Shift-fine/double-click-reset/wheel, selectors, switches), per-slot
  bypass & mix, A/B compare, MIDI learn, presets + crash recovery, a full
  **module reference** with every parameter and range for all 8 modules, a
  keyboard/mouse quick reference, and troubleshooting (incl. the standalone
  input-device-mismatch gotcha). Parameter tables verified against the module
  descriptors and `declareParameters`.
- README status table now shows the whole plan complete and links the manual.

### Verified

- All parameter ranges/U-heights in the reference cross-checked against
  `src/dsp/*/…Module.*` (descriptors + `declareParameters`) and
  `ui/src/bridge/previewSchema.ts`.

### Remaining (external, not milestones)

Signed + notarised release builds; a universal macOS binary; the Windows
installer compiled + smoke-tested on a real Windows host; a host-automation
hands-on pass in a real DAW.

## 2026-08-18 (M8b) — installers + signing/notarisation docs

Second slice of M8 (Ship). See ADR 0008.

### Done

- **macOS installer** (`packaging/macos/`) — `build_pkg.sh` builds one
  `pkgbuild` component package per format (VST3 → `/Library/Audio/Plug-Ins/VST3`,
  AU → `.../Components`, Standalone → `/Applications`) and wraps them with
  `productbuild` + `distribution.xml` into a single customisable `.pkg` (three
  deselectable choices, AGPLv3 licence pane, welcome/conclusion). Signing +
  notarisation are opt-in via `APP_SIGN_ID` / `INSTALLER_SIGN_ID` /
  `NOTARY_PROFILE`, so the same script produces an unsigned dev build or a
  signed+notarised release build.
- **Windows installer** (`packaging/windows/audiorack.iss`) — Inno Setup script:
  VST3 → `Common Files\VST3`, standalone → Program Files, both deselectable;
  bundles and silently installs the Edge **WebView2** runtime only when absent
  (`NeedsWebView2`). Bootstrapper is git-ignored, not vendored.
- **Signing/notarisation runbook** (`docs/SIGNING.md`) — Developer ID
  Application/Installer + `notarytool`/`stapler` for macOS; `signtool` for
  Windows; hardened-runtime mic entitlement note; release checklist.

### Verified

- **Unsigned macOS `.pkg` built and inspected in-repo**: 12 MB, three component
  packages with correct identifiers and `install-location`s (via
  `pkgutil --expand` + `PackageInfo`). VST3/AU/Standalone all present.
- Windows `.iss` and all signing/notarisation steps are **documented but not
  exercised here** (no Windows host, no paid Apple Developer account, no full
  Xcode) — see the note in `docs/SIGNING.md`.

### Next (M8c)

User manual: install, the rack UX, every module + parameters, MIDI learn, A/B,
presets. Still open: signed/notarised release builds; a universal macOS binary;
the Windows installer compiled on a real Windows machine; host-automation
hands-on pass in a real DAW.

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
