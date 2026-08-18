# AudioRack — Claude Code Project Brief

> **How to use this file:** save it as `CLAUDE.md` at the repo root. Start Claude Code in an
> empty directory and say: *"Read CLAUDE.md. Confirm the stack decisions in §2, then implement
> Milestone M0 only. Stop and show me the result before continuing."*
> Work **one milestone per session**. Never ask for "build the whole app".

---

## 1. Product

**AudioRack** is an audio effects host shaped like a virtual 19" equipment rack.

The user composes a rack by dragging modules (compressors, limiters, EQs, delays, reverbs,
saturators, gates, meters…) into rack slots. Signal flows top-to-bottom through the mounted
units. It ships as:

- a **standalone desktop application** (own audio device I/O, macOS + Windows), and
- a **plugin**: VST3 and AudioUnit (v2), built from the same codebase.

Two non-negotiable product qualities:

1. **Visual accuracy.** Units must look like real hardware — brushed/anodised faceplates,
   recessed screws, backlit VU meters with correct needle ballistics, LED ladders, knobs with
   real detents and pointer shadows, silkscreened labels. Not flat design. Not "material".
2. **Interactivity.** Every control responds like hardware: fine-drag with modifier keys,
   double-click to reset, mouse-wheel, keyboard entry, host automation, MIDI learn. Meters
   run at display refresh rate without ever touching the audio thread.

---

## 2. Stack decisions — confirm before writing code

**Primary (assume this unless I say otherwise):**

| Layer | Choice | Why |
|---|---|---|
| Core / DSP | **C++20**, no exceptions or RTTI on the audio path | Only realistic path to VST3/AU |
| Framework | **JUCE 8** (fetched via CMake `FetchContent`) | Plugin wrappers, device I/O, param system |
| Build | **CMake ≥ 3.22** + Ninja, presets in `CMakePresets.json` | Reproducible, CI-friendly |
| Plugin formats | VST3, AU, Standalone (LV2 optional later) | |
| **UI** | **`juce::WebBrowserComponent`** hosting a bundled TypeScript SPA | Lets the rack UI be built with web tooling; SVG/CSS gives the photoreal look cheaply |
| UI build | Vite + TypeScript (vanilla or Lit; **no heavy SPA framework** — no router, no zone.js) | Bundle must be a single small `index.html` + assets embedded as `BinaryData` |
| Native↔UI bridge | JUCE 8 `WebSliderRelay` / `WebBrowserComponent::Options::withNativeIntegrationEnabled` | Typed, no ad-hoc string protocols |
| Tests | Catch2 v3 for DSP, `pluginval` for host compliance | |

**Alternative (only if I say "go native"):** pure JUCE `Component` tree with an OpenGL context,
knob/meter graphics as filmstrip PNGs or `Drawable` SVGs. Lower latency UI, heavier to author.

**Legal — surface this to me before M0 completes:** JUCE is AGPLv3 or commercial; the Steinberg
VST3 SDK requires accepting its licence and (for distribution) registering. State clearly in the
README which licence path the project assumes.

---

## 3. Architecture

```
audiorack/
├─ CMakeLists.txt, CMakePresets.json
├─ src/
│  ├─ core/            # engine, no JUCE UI deps
│  │  ├─ RackEngine.*        # ordered module chain, processes blocks
│  │  ├─ RackSlot.*          # position, U-height, bypass, wet/dry
│  │  ├─ ModuleRegistry.*    # id → factory, self-registering modules
│  │  ├─ AudioModule.h       # abstract base (see §4)
│  │  └─ ParameterModel.*    # namespaced param IDs, ranges, units
│  ├─ dsp/             # one folder per module, header + impl + tests
│  │  ├─ compressor/ limiter/ eq/ delay/ reverb/ gate/ saturator/ meter/
│  │  └─ common/       # SmoothedValue, envelope followers, filters, oversampler,
│  │                   # delay lines, allpass/comb, dither, DCBlocker
│  ├─ state/           # preset & rack serialisation (JSON), migration by schema version
│  ├─ ui/              # WebBrowserComponent host, relays, resource provider
│  └─ plugin/          # AudioProcessor + AudioProcessorEditor
├─ ui/                 # TypeScript app (Vite) — built into src/ui/dist, embedded as BinaryData
│  ├─ src/rack/        # rack frame, slot layout, drag & drop, cable/routing overlay
│  ├─ src/units/       # one component per module faceplate
│  ├─ src/widgets/     # Knob, Fader, VuMeter, LedLadder, Switch, Button, Screw, Display
│  └─ src/bridge/      # typed wrapper over JUCE native integration
├─ tests/              # Catch2 DSP tests + offline render harness
├─ assets/             # SVG faceplates, textures, fonts, filmstrips
└─ docs/adr/           # one Architecture Decision Record per significant choice
```

### Hard rules for the audio thread

- No allocation, no locks, no `std::string`, no logging, no file I/O, no `std::function` that
  may allocate, no exceptions in `processBlock` or anything it calls.
- Preallocate everything in `prepareToPlay(sampleRate, maxBlockSize)`. Assume block size and
  sample rate can change at any time.
- UI → audio: parameter changes go through `AudioProcessorValueTreeState` atomics + per-block
  `SmoothedValue` ramps. Structural changes (add/remove/reorder a module) go through a
  **lock-free command queue** consumed at the top of `processBlock`; the deleted object is
  handed back to the message thread for destruction, never `delete`d on the audio thread.
- Audio → UI: a single-producer/single-consumer ring buffer of meter frames (peak, RMS, GR),
  polled by a UI timer at ~60 Hz. Meters must be correct even if a block is dropped.
- Denormals: `ScopedNoDenormals` plus explicit flush in feedback structures.
- Latency: every module reports its own latency; `RackEngine` sums it and calls
  `setLatencySamples`. Lookahead limiters and oversamplers must report honestly.

---

## 4. The module contract

```cpp
class AudioModule {
public:
    virtual ~AudioModule() = default;

    // Identity (static, used by registry, presets, and UI)
    virtual ModuleDescriptor descriptor() const = 0;   // id, name, category, rackUnits (1U/2U/3U)

    // Lifecycle — message thread
    virtual void prepare (double sampleRate, int maxBlock, int numChannels) = 0;
    virtual void reset() = 0;

    // Audio thread — must be realtime-safe
    virtual void process (juce::dsp::AudioBlock<float>&, const TransportInfo&) noexcept = 0;

    virtual int  latencySamples() const noexcept { return 0; }
    virtual void getMeterFrame (MeterFrame&) const noexcept {}

    // Parameters are declared once, statically, and owned by the processor's APVTS
    static void declareParameters (ParameterBuilder&, const juce::String& instanceId);
};
```

- Parameter IDs are namespaced: `slot3.comp.threshold`. Stable across versions — **never rename
  a shipped parameter ID**; deprecate and add.
- Adding a new module must require: a new folder in `src/dsp/`, a registry line, a faceplate
  component in `ui/src/units/`. Nothing else. If it requires touching the engine, the
  abstraction is wrong.

---

## 5. Module set — v1

Each needs: correct DSP, tests, a faceplate, and a short `docs/modules/<name>.md` explaining the
algorithm and its parameters.

| Module | Key requirements |
|---|---|
| **Compressor** | Feed-forward + feed-back modes, peak/RMS detection, soft knee (dB-domain), programme-dependent release option, sidechain HPF, external sidechain input, auto-makeup, GR metering |
| **Limiter** | True-peak aware, lookahead (report latency!), ISP-safe ceiling, 4× oversampled detection |
| **Parametric EQ** | 6 bands, bell/shelf/HP/LP, RBJ or SVF coefficients, live magnitude response curve in UI, band solo |
| **Delay** | Digital + tape modes, host-sync'd note divisions, ping-pong, feedback filtering, wow/flutter, fractional-delay interpolation (Lagrange/allpass), no zipper noise on time changes |
| **Reverb** | FDN (8×8 Hadamard) or Dattorro plate, size/decay/damping/predelay/modulation, freeze |
| **Gate / Expander** | Hysteresis, hold, range, sidechain filter, lookahead |
| **Saturator** | Tube/tape/transistor curves, oversampled ≥4× with anti-imaging filters, drive-compensated output |
| **Meter / Analyzer** | Peak/RMS/LUFS-M/S/I, correlation, FFT spectrum, VU with 300 ms ballistics |
| **Utility** | Gain, pan, width, phase invert, mono-ify — 1U |

---

## 6. The rack UI

- Rack frame with correct proportions: 19" width, 1U = 1.75", mounting rail with square holes,
  visible rack ears and screws. Modules snap to U boundaries.
- Drag a module from a browser panel into a slot; drag to reorder; right-click for
  bypass/remove/duplicate; **A/B** compare; per-slot wet/dry and bypass with click-free crossfade.
- Vertical scroll for tall racks; racks persist in presets.
- Widgets:
  - **Knob**: SVG pointer + shadow, drag-vertical, Shift = fine (×0.1), double-click = default,
    wheel support, value tooltip on hover, `aria` labels.
  - **VU meter**: real ballistics (300 ms rise/fall, ~1% overshoot), backlit face, needle
    inertia, peak LED.
  - **LED ladder**: per-segment gradient, hold-peak, correct dB spacing.
  - All widgets driven from a typed store; no direct DOM poking from the bridge layer.
- Rendering budget: **< 4 ms per frame** for a full 12-unit rack. Use `requestAnimationFrame`,
  batch meter updates, avoid layout thrash, prefer CSS transforms over re-render.
- Editor must be resizable with a constrained aspect ratio, and remember its size in state.

---

## 7. Milestones — implement strictly in order, one per session

- **M0 — Skeleton.** CMake project, JUCE fetched, builds Standalone + VST3 + AU that passes
  audio through unmodified. `pluginval --strictness-level 10` passes. CI workflow on
  macOS + Windows. README with licence notes. *Nothing else.*
- **M1 — Engine.** `RackEngine`, `AudioModule`, `ModuleRegistry`, lock-free command queue,
  latency reporting. One trivial module (Gain). Unit tests for the queue under thread sanitizer.
- **M2 — Parameters & state.** APVTS wiring, dynamic per-slot parameter namespacing, preset
  save/load with schema version + migration, host automation verified in a real DAW.
- **M3 — UI shell.** WebView host, resource provider, typed bridge, empty rack frame, one
  faceplate for Gain, meter ring buffer end-to-end. Prove the 4 ms budget with a profiler trace.
- **M4 — Widget library.** Knob, fader, VU, LED ladder, switch, display. Storybook-style
  dev page under `ui/dev/` so widgets can be built without launching the plugin.
- **M5 — Core dynamics.** Compressor + Limiter + Gate, with tests: static curve verification,
  attack/release timing within tolerance, null test against a reference render.
- **M6 — Time & tone.** EQ, Delay, Reverb, Saturator. Oversampling helper. Host transport sync.
- **M7 — Rack UX.** Drag & drop, reorder, A/B, browser panel, resizing, MIDI learn.
- **M8 — Ship.** Installers (pkg/InnoSetup), code signing + notarisation docs, crash-safe state,
  performance pass, user manual.

**Definition of done for every milestone:** builds clean on both platforms with warnings-as-errors,
tests pass, `pluginval` strictness 10 passes, no ASan/TSan/UBSan findings, an ADR is written for
any non-obvious decision, and the milestone is a single reviewable commit.

---

## 8. Working agreement with Claude Code

- **Ask before assuming.** If a design choice has real trade-offs (detector topology, reverb
  algorithm, how slots map to parameter IDs), present 2–3 options with trade-offs and wait.
- **Small diffs.** Never rewrite files wholesale. Never touch files outside the current milestone.
- **Write the test first** for anything in `src/dsp/`. A DSP claim without a test is a guess.
- **Measure, don't assert.** Any performance or latency claim must come with a number produced
  by a benchmark or profiler run in this repo.
- **No placeholder DSP.** Don't stub a reverb with a comment saying "real algorithm later".
  If it's not implemented, it's not in the registry.
- **Explain the maths.** When implementing a filter or detector, put the derivation or the
  reference (Zölzer, Reiss & McPherson, Pirkle, RBJ cookbook, Dattorro's plate paper) in a
  comment at the top of the file.
- At the end of each session, update `docs/PROGRESS.md`: what's done, what's next, what's
  uncertain.

---

## 9. Open questions to answer at M0

1. Target platforms — macOS Universal + Windows x64 only, or Linux too?
2. Minimum macOS version and whether AAX matters (needs Avid approval).
3. WebView UI or native JUCE components? (§2)
4. Licence path: AGPL and open-source the whole thing, or JUCE commercial?
5. Is the rack strictly a serial chain in v1, or do we need parallel/sidechain routing at M7?
6. Sidechain input: does the plugin expose an extra input bus from day one?
