# Windows redistributables

Place **`MicrosoftEdgeWebview2Setup.exe`** (the Evergreen *bootstrapper*) in this
folder before compiling `audiorack.iss`. It is a Microsoft binary and is **not**
checked into this repository.

Download it from:
<https://developer.microsoft.com/microsoft-edge/webview2/> → *Evergreen
Bootstrapper*.

The installer runs it silently (`/silent /install`) only when no WebView2
runtime is already registered on the machine (see `NeedsWebView2` in the `.iss`).
WebView2 ships with Windows 11 and current Windows 10, so on most machines this
is a no-op — but bundling it guarantees the AudioRack UI can render everywhere.
