# AudioRack

An audio effects host shaped like a virtual 19" equipment rack. Drag modules
(compressors, limiters, EQs, delays, reverbs, saturators, gates, meters…) into
rack slots; signal flows top-to-bottom through the mounted units.

Ships as a **standalone desktop application** and as a **VST3 / AudioUnit (v2)
plugin** from the same codebase. Targets: **macOS 11+ (universal)** and
**Windows 10+ x64**.

## Status

| Milestone | State |
|---|---|
| M0 — Skeleton (passthrough Standalone/VST3/AU, CI) | ✅ |
| M1 — Engine (RackEngine, module registry, command queue) | ✅ |
| M2 — Parameters & state | ✅ |
| M3 — WebView UI shell | ✅ |
| M4 — Widget library | ✅ |
| M5 — Core dynamics (compressor, limiter, gate) | ✅ |
| M6 — Time & tone (EQ, delay, reverb, saturator) | ✅ |
| M7a — Rack UX: browser, drag & drop, reorder, duplicate, A/B | ✅ |
| M7b — MIDI learn (right-click any knob/switch) | ✅ |
| M8 — Shipping (installers, signing, manual) | ⏳ |

See [docs/PROGRESS.md](docs/PROGRESS.md).

## Building

Requirements: CMake ≥ 3.22, Ninja (macOS), Visual Studio 2022 (Windows),
Node ≥ 18 (UI bundle). JUCE 8 and Catch2 are fetched automatically by CMake.

```sh
# macOS
cmake --preset macos
cmake --build --preset macos

# Windows (x64)
cmake --preset windows
cmake --build --preset windows
```

Artefacts land in `build/<preset>/AudioRack_artefacts/`. Tests:

```sh
ctest --preset macos
```

The web UI is developed independently under `ui/` (`npm run dev` for the
widget dev page, `npm run build` to produce the bundle embedded into the
plugin).

## Licence

**This project is open source under the GNU Affero General Public License v3
(AGPLv3).** See [LICENSE](LICENSE).

Third-party licence notes:

- **JUCE 8** is used under its **AGPLv3** option (not the commercial licence).
  Consequently this entire codebase is and must remain AGPLv3-compatible, and
  the JUCE splash screen is disabled as permitted under that option. Anyone
  distributing a closed-source derivative would need their own JUCE commercial
  licence.
- **VST3**: the Steinberg VST3 SDK (bundled by JUCE) is dual-licensed; this
  project uses it under its **GPLv3** option, which is compatible with AGPLv3.
  Distributing binaries that use the *proprietary* Steinberg licence instead
  requires signing Steinberg's agreement and registering with Steinberg.
  "VST" is a trademark of Steinberg Media Technologies GmbH.
- **AudioUnit** is an Apple technology; no additional licence is required.

## Repository layout

```
src/core/     engine (no JUCE UI deps)        src/dsp/    one folder per module
src/state/    preset serialisation            src/ui/     WebView host + bridge
src/plugin/   AudioProcessor / Editor         ui/         TypeScript SPA (Vite)
tests/        Catch2 + offline render harness docs/adr/   architecture decisions
```
