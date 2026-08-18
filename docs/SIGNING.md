# Code signing & notarisation

Distributing AudioRack outside your own machine requires signing on both
platforms, and notarisation on macOS. None of this is done in CI or by default —
the build produces **ad-hoc-signed** binaries (fine for local use, not for
distribution). This document is the checklist for a real release.

> The dev machine used to build this repo has no full Xcode and no paid Apple
> Developer account, so the signing/notarisation steps below are documented and
> the scripts wired to accept credentials, but have not been exercised end to
> end here. The unsigned `.pkg` build path *is* verified.

---

## macOS

### Prerequisites

- A paid **Apple Developer** account.
- Two certificates in your login keychain (from Xcode → Settings → Accounts →
  Manage Certificates, or the Developer portal):
  - **Developer ID Application** — signs the `.vst3`, `.component`, `.app`.
  - **Developer ID Installer** — signs the `.pkg`.
- A **notarytool keychain profile** storing an app-specific password:

  ```sh
  xcrun notarytool store-credentials AudioRackNotary \
      --apple-id "you@example.com" \
      --team-id  "TEAMID" \
      --password "app-specific-password"
  ```

### Build a universal, signed, notarised installer

```sh
# 1. Universal Release build (arm64 + x86_64).
cmake --preset macos-universal
cmake --build --preset macos-universal

# 2. Sign bundles + installer and notarise, all in one go.
export APP_SIGN_ID="Developer ID Application: Your Name (TEAMID)"
export INSTALLER_SIGN_ID="Developer ID Installer: Your Name (TEAMID)"
export NOTARY_PROFILE="AudioRackNotary"

packaging/macos/build_pkg.sh \
    build/macos-universal/src/AudioRack_artefacts/Release
```

`build_pkg.sh` then:

1. `codesign --force --deep --options runtime --timestamp` each bundle
   (hardened runtime + secure timestamp are required for notarisation).
2. `pkgbuild` a component package per format → standard install locations.
3. `productbuild --sign "$INSTALLER_SIGN_ID"` the product archive.
4. `xcrun notarytool submit --wait` then `xcrun stapler staple` the `.pkg`.

### Verify

```sh
spctl  --assess --type install -vv build/installer/macos/AudioRack-*.pkg   # accepted: notarized
pkgutil --check-signature          build/installer/macos/AudioRack-*.pkg
codesign --verify --deep --strict --verbose=2 \
    "/Library/Audio/Plug-Ins/VST3/AudioRack.vst3"
```

### Hardened runtime & entitlements

The standalone requests microphone access (`NSMicrophoneUsageDescription` is set
via `MICROPHONE_PERMISSION_ENABLED` in `src/CMakeLists.txt`). Under the hardened
runtime that also needs the microphone entitlement; JUCE's standalone target
sets `com.apple.security.device.audio-input` by default. If a custom entitlements
file is used, include that key or input capture is denied.

---

## Windows

### Prerequisites

- A code-signing certificate (OV or EV; EV avoids SmartScreen warnings), usable
  by `signtool`.
- [Inno Setup 6](https://jrsoftware.org/isinfo.php) on `PATH` (`iscc`).
- `packaging/windows/redist/MicrosoftEdgeWebview2Setup.exe` present
  (see that folder's README).

### Sign the binaries, then the installer

```bat
:: 1. Build.
cmake --preset windows
cmake --build --preset windows

:: 2. Sign the plug-in + standalone (timestamped).
set ART=build\windows\src\AudioRack_artefacts\Release
signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 ^
    "%ART%\VST3\AudioRack.vst3\Contents\x86_64-win\AudioRack.vst3" ^
    "%ART%\Standalone\AudioRack.exe"

:: 3. Build the installer (configure a Sign Tool named "signtool" in Inno Setup).
iscc /DSIGN=1 packaging\windows\audiorack.iss

:: 4. Sign the installer itself.
signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 ^
    packaging\windows\Output\AudioRack-0.1.0-Setup.exe
```

### Verify

```bat
signtool verify /pa /v packaging\windows\Output\AudioRack-0.1.0-Setup.exe
```

---

## Release checklist

- [ ] Bump `project(... VERSION x.y.z)` in `CMakeLists.txt` **and**
      `AppVersion` in `packaging/windows/audiorack.iss` (or pass
      `AUDIORACK_VERSION` to `build_pkg.sh`).
- [ ] macOS: universal build, signed + notarised `.pkg`, `spctl` accepted.
- [ ] Windows: signed binaries + signed installer, `signtool verify` clean.
- [ ] Smoke-test each artefact on a clean machine (DAW rescan + standalone launch).
- [ ] Tag the release and attach both installers.
