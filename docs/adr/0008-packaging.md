# ADR 0008 — Installers & packaging (M8b)

Date: 2026-08-18 · Status: accepted

M8b delivers the distributable installers for both platforms, plus the signing
and notarisation runbook. Crash-safe state and the performance pass were M8a
(ADR 0007); the user manual is M8c.

## Decisions

### Scripts, not CPack

Installers are plain scripts under `packaging/`, not CMake/CPack targets.
Packaging plug-in **bundles** (`.vst3`, `.component`, `.app`) to their OS-specific
locations, with per-format install choices and a WebView2 dependency on Windows,
is expressed far more directly in `pkgbuild`/`productbuild` and Inno Setup than
in CPack generators. Keeping packaging out of the CMake graph also means a
plain build never drags in installer logic.

### macOS: a customisable product archive

`packaging/macos/build_pkg.sh` builds one `pkgbuild` **component package per
format**, each with its standard `--install-location`:

| Format | Location |
|---|---|
| VST3 | `/Library/Audio/Plug-Ins/VST3` |
| Audio Unit | `/Library/Audio/Plug-Ins/Components` |
| Standalone | `/Applications` |

`productbuild --distribution distribution.xml` wraps them into one `.pkg` whose
three choices the user can deselect. The AGPLv3 `LICENSE` is copied in as the
installer's licence pane at build time (single source of truth).

Signing/notarisation is **opt-in via environment variables**
(`APP_SIGN_ID`, `INSTALLER_SIGN_ID`, `NOTARY_PROFILE`) so the same script builds
an unsigned installer for local testing and a signed+notarised one for release —
no separate code path. When set it runs `codesign --options runtime --timestamp`
on the bundles, `productbuild --sign` on the archive, then
`notarytool submit --wait` + `stapler staple`.

### Windows: Inno Setup + WebView2

`packaging/windows/audiorack.iss` installs the x64 VST3 bundle folder to
`Common Files\VST3` and the standalone to Program Files, as deselectable
components. Because the UI is a WebView2-hosted SPA, the installer bundles the
Microsoft Edge **WebView2 Evergreen bootstrapper** and runs it silently — but
only when `NeedsWebView2` finds no runtime registered (it ships with Win11 and
current Win10, so usually a no-op). The bootstrapper is a Microsoft binary and
is git-ignored, not vendored.

## Verification

The **unsigned macOS `.pkg` path is verified in-repo**: `build_pkg.sh` produces a
12 MB archive whose three component packages carry the correct identifiers and
`install-location`s (checked with `pkgutil --expand` + `PackageInfo`). The
Windows script and all signing/notarisation steps are documented in
[SIGNING.md](../SIGNING.md) but not exercised on the dev machine (no Windows
host, no paid Apple Developer account, no full Xcode) — see the note there.

## What's still open

- Actual signed + notarised release builds (needs the accounts above).
- A universal macOS binary for shipping (`macos-universal` preset exists; the
  local Intel build is x86_64-only).
- Windows installer compiled and smoke-tested on a real Windows machine.
