# Progress

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
