# ADR 0001 — Stack, target platforms, licence path

Date: 2026-08-18 · Status: accepted

## Decisions (answers to the §9 open questions of the project brief)

1. **Platforms:** macOS 11+ (universal arm64 + x86_64) and Windows 10+ x64.
   No Linux in v1; nothing in the architecture precludes adding it later.
2. **No AAX.** Avid approval is out of scope for v1.
3. **UI:** JUCE 8 `WebBrowserComponent` hosting a bundled TypeScript SPA
   (Vite, no heavy framework), with `withNativeIntegrationEnabled` relays as
   the bridge. Rationale: the photoreal faceplate look is dramatically cheaper
   to author and iterate in SVG/CSS than in native `Graphics` code, and the
   widget library can be developed in a browser without launching a host.
4. **Licence:** fully open source under **AGPLv3**, using JUCE's AGPL option
   and the VST3 SDK's GPLv3 option. Splash screen disabled (permitted under
   AGPL). A future closed-source fork would need JUCE commercial + Steinberg
   proprietary agreements.
5. **Routing:** the rack is a strictly **serial chain in v1**. `RackEngine`
   processes an ordered list; parallel/sidechain graph routing is deliberately
   deferred so the engine stays trivially verifiable.
6. **Sidechain:** the plugin declares a stereo external sidechain input bus
   **from M0**, initially disabled and unused. Bus layouts are effectively
   append-only once a plugin ships (hosts persist them in sessions), so the
   layout is fixed before first release rather than changed at M5.

## Build-environment caveat (local dev machine)

The local Mac has Command Line Tools with Apple clang 12.0.5 / macOS 11.3 SDK
(no full Xcode). This is the oldest toolchain JUCE 8 plausibly accepts; CI
uses current Xcode on `macos-14` and is the reference for "builds clean".
C++20 usage in first-party code stays conservative (no ranges, no concepts)
until the local toolchain is updated.
