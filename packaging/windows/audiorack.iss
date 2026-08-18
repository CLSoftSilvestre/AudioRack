; Inno Setup script for AudioRack (Windows x64).
;
; Build the plugin first:  cmake --build --preset windows
; Then compile this installer with Inno Setup 6:
;   iscc packaging\windows\audiorack.iss
; Output: packaging\windows\Output\AudioRack-<version>-Setup.exe
;
; Signing: see docs/SIGNING.md. Set SignTool in Inno Setup and pass
;   /DSIGN=1  to sign the installer and the bundled binaries.
;
; The x64 VST3 that JUCE builds is a *bundle folder* (AudioRack.vst3\Contents\
; x86_64-win\AudioRack.vst3), so it is installed recursively.

#define AppName "AudioRack"
#define AppVersion "0.1.0"
#define AppPublisher "AudioRack Project"
#define AppURL "https://github.com/celsosilvestre/audiorack"
; Release artefacts from the `windows` preset.
#define Artefacts "..\..\build\windows\src\AudioRack_artefacts\Release"

[Setup]
AppId={{A0D1O-RACK-0001-VST3-STANDALONE}}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputDir=Output
OutputBaseFilename=AudioRack-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; 64-bit only.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

[Types]
Name: "full";   Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 Plug-in";           Types: full custom
Name: "standalone"; Description: "Standalone Application";  Types: full custom

[Files]
; VST3 bundle -> common VST3 folder (C:\Program Files\Common Files\VST3).
Source: "{#Artefacts}\VST3\AudioRack.vst3\*"; DestDir: "{commoncf64}\VST3\AudioRack.vst3"; \
    Components: vst3; Flags: recursesubdirs createallsubdirs ignoreversion

; Standalone executable -> Program Files\AudioRack.
Source: "{#Artefacts}\Standalone\AudioRack.exe"; DestDir: "{app}"; \
    Components: standalone; Flags: ignoreversion

; WebView2 Evergreen bootstrapper. Download once from
;   https://developer.microsoft.com/microsoft-edge/webview2/
; and place it here before compiling. Extracted to {tmp} and run silently below.
Source: "redist\MicrosoftEdgeWebview2Setup.exe"; DestDir: "{tmp}"; Flags: dontcopy

[Icons]
Name: "{group}\AudioRack";              Filename: "{app}\AudioRack.exe"; Components: standalone
Name: "{autodesktop}\AudioRack";        Filename: "{app}\AudioRack.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Components: standalone

[Run]
; Ensure the WebView2 runtime is present (no-op if already installed). The UI is
; a WebView2-hosted SPA, so this is required for the plug-in and app to render.
Filename: "{tmp}\MicrosoftEdgeWebview2Setup.exe"; Parameters: "/silent /install"; \
    StatusMsg: "Installing Microsoft Edge WebView2 runtime..."; \
    Check: NeedsWebView2; Flags: waituntilterminated

Filename: "{app}\AudioRack.exe"; Description: "Launch AudioRack"; \
    Flags: nowait postinstall skipifsilent; Components: standalone

[Code]
{ Skip the WebView2 install when a runtime is already registered (per-machine
  or per-user), so repeat installs don't re-run the bootstrapper needlessly. }
function NeedsWebView2: Boolean;
var
  Version: String;
begin
  Result := not (
    RegQueryStringValue(HKLM,
      'SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
      'pv', Version) or
    RegQueryStringValue(HKCU,
      'SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
      'pv', Version));
end;
