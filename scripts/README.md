# scripts

Convenience wrappers over the CMake presets and packaging.

| Script | What it does |
|---|---|
| `build.sh` | Build **Standalone + VST3 + AU** (macOS, Release by default). |
| `deploy.sh` | Build a **universal Release** and package the shippable `.pkg`. |

## Build

```sh
scripts/build.sh                 # Release build of all three formats
scripts/build.sh --debug         # Debug build
scripts/build.sh --clean --tests # clean rebuild, then run the test suite
scripts/build.sh --open          # reveal the artefacts in Finder
```

Artefacts land in `build/macos/src/AudioRack_artefacts/Release/`
(`Standalone/AudioRack.app`, `VST3/AudioRack.vst3`, `AU/AudioRack.component`).

## Deploy (final version)

```sh
scripts/deploy.sh                # unsigned universal .pkg (local testing)
```

For a distributable, notarised installer, export your credentials first
(see [../docs/SIGNING.md](../docs/SIGNING.md)):

```sh
export APP_SIGN_ID="Developer ID Application: Your Name (TEAMID)"
export INSTALLER_SIGN_ID="Developer ID Installer: Your Name (TEAMID)"
export NOTARY_PROFILE="AudioRackNotary"
scripts/deploy.sh                # signs, packages, notarises, staples
```

The installer is written to `build/installer/macos/AudioRack-<version>.pkg`.

`PRESET=macos scripts/deploy.sh` packages a quick native-arch-only build instead
of the universal one.

> **Windows:** build with `cmake --preset windows && cmake --build --preset
> windows`, then compile `packaging/windows/audiorack.iss` with Inno Setup.
