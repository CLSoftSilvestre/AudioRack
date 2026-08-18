# Progress

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
