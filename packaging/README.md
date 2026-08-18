# Packaging

Installer tooling for AudioRack. Signing and notarisation details live in
[../docs/SIGNING.md](../docs/SIGNING.md).

## macOS — `.pkg`

```sh
cmake --preset macos && cmake --build --preset macos     # or macos-universal
packaging/macos/build_pkg.sh
# -> build/installer/macos/AudioRack-<version>.pkg
```

A customisable product archive with three deselectable choices, each installed
to its standard location:

| Format | Location |
|---|---|
| VST3 | `/Library/Audio/Plug-Ins/VST3` |
| Audio Unit | `/Library/Audio/Plug-Ins/Components` |
| Standalone | `/Applications` |

Signing/notarisation is opt-in via env vars (`APP_SIGN_ID`,
`INSTALLER_SIGN_ID`, `NOTARY_PROFILE`); unset, it builds an unsigned installer
for local testing.

- `build_pkg.sh` — component packages + `productbuild`.
- `distribution.xml` — installer choices, licence, welcome/conclusion.
- `resources/` — `welcome.html`, `conclusion.html` (the AGPLv3 `LICENSE` is
  copied in at build time as `license.txt`).

## Windows — Inno Setup

```bat
cmake --preset windows && cmake --build --preset windows
iscc packaging\windows\audiorack.iss
:: -> packaging\windows\Output\AudioRack-<version>-Setup.exe
```

Installs the VST3 to `Common Files\VST3` and the standalone to Program Files,
and silently installs the Microsoft Edge **WebView2** runtime when absent (the
UI is a WebView2-hosted SPA). Drop `MicrosoftEdgeWebview2Setup.exe` into
`windows/redist/` first — see that folder's README.

Installer outputs and the downloaded WebView2 bootstrapper are git-ignored.
