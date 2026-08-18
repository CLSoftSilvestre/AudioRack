# AudioRack — User Manual

AudioRack is an audio effects host shaped like a virtual 19″ equipment rack. You
build a rack by dragging modules — compressors, limiters, EQs, delays, reverbs,
saturators, gates — into slots. Signal flows **top to bottom** through the
mounted units.

It runs two ways from the same engine:

- a **standalone application** (its own audio device I/O), and
- a **plug-in**: VST3 and Audio Unit, loaded inside a DAW.

---

## Contents

1. [Installing](#1-installing)
2. [First launch](#2-first-launch)
3. [The rack at a glance](#3-the-rack-at-a-glance)
4. [Building a rack](#4-building-a-rack)
5. [Working with controls](#5-working-with-controls)
6. [Per-slot bypass & mix](#6-per-slot-bypass--mix)
7. [A/B compare](#7-ab-compare)
8. [MIDI learn](#8-midi-learn)
9. [Presets, saving & crash recovery](#9-presets-saving--crash-recovery)
10. [Module reference](#10-module-reference)
11. [Keyboard & mouse quick reference](#11-keyboard--mouse-quick-reference)
12. [Troubleshooting](#12-troubleshooting)

---

## 1. Installing

### macOS

Run **AudioRack-*.pkg** and follow the installer. You can deselect any format
you don't need; the rest install to their standard locations:

| Format | Location |
|---|---|
| VST3 | `/Library/Audio/Plug-Ins/VST3` |
| Audio Unit | `/Library/Audio/Plug-Ins/Components` |
| Standalone app | `/Applications` |

Requires macOS 11 or later.

### Windows

Run **AudioRack-*-Setup.exe**. It installs the VST3 to `Common Files\VST3` and
the standalone to Program Files, and installs the Microsoft Edge **WebView2**
runtime if your machine doesn't already have it (the interface needs it to
render). Requires 64-bit Windows 10 or later.

After installing, **rescan plug-ins** (or restart) in your DAW so it picks up
AudioRack.

---

## 2. First launch

### Standalone

1. Open **AudioRack** from Applications (macOS) or the Start menu (Windows).
2. Click **Options ▸ Audio/MIDI Settings** and choose your input and output
   device, sample rate, and buffer size.
3. The first time it opens an input, macOS asks for **microphone permission** —
   allow it, or the app captures only silence.

> **Tip (macOS):** pick input and output devices that share a clock. Mixing a
> built-in microphone input with a Bluetooth (e.g. AirPods) output forces macOS
> to build an aggregate device, which is unreliable and can leave the input
> silent. Use built-in in **and** out, or a single interface, for a clean signal.

### Plug-in

Add **AudioRack** as an effect on a track in your DAW. It presents a stereo
input/output and an optional **stereo sidechain** input bus (used by the
Compressor and Gate when set to *External*). The editor is resizable; drag a
corner to taste.

---

## 3. The rack at a glance

```
┌─────────────┬──────────────────────────────────────────────┐
│  MODULES    │  AUDIORACK        COMPARE [A][B]  [COPY →]     │  ← toolbar
│  (browser)  ├──────────────────────────────────────────────┤
│  Gain    1U │  ▓▓  mounted unit (faceplate)              ▓▓  │  ← slot
│  Compressor │  ▓▓  mounted unit                          ▓▓  │
│  …          │  ▓▓  empty slot — double-click to mount    ▓▓  │
│  Reverb  3U │        …                                       │
│             │                                                │
│ DRAG A UNIT │                                                │
│ HERE TO     │                                                │
│ REMOVE      │                                                │
└─────────────┴──────────────────────────────────────────────┘
```

- **Module browser** (left): every module, grouped by category, with its rack
  height (1U/2U/3U). This is also the **"drop here to remove"** zone.
- **Toolbar** (top): the AudioRack wordmark and the **A / B / COPY** controls.
- **Rack** (right): twelve slots between mounting posts. Units snap to slots and
  signal flows top-to-bottom. The rack scrolls vertically for tall stacks and
  scales to fill the window width.

---

## 4. Building a rack

**Mount a module** — any of:

- **Drag** a module from the browser onto an empty slot.
- **Double-click** an empty slot and pick a module.
- **Double-click** a module chip in the browser to drop it in the first free slot.

**Reorder** — grab a mounted unit by its **rack ears** (the screwed tabs at the
far left/right of the faceplate) and drag it onto another slot. Dropping on an
occupied slot **swaps** the two.

**Duplicate** — right-click a unit ▸ **Duplicate**. A copy, with all its values,
lands in the first empty slot.

**Remove** — right-click a unit ▸ **Remove**, or drag it onto the browser panel
(the panel highlights as a drop-to-remove target).

Parameter *values* travel with a module when you move or duplicate it; DSP tails
(reverb/delay memory) deliberately reset so moving a unit never clicks.

---

## 5. Working with controls

Every control behaves like hardware.

**Knobs**

| Action | Result |
|---|---|
| Drag up / down | Change value |
| **Shift** + drag | Fine adjust (×0.1) |
| Double-click | Reset to default |
| Mouse wheel | Step (Shift = fine) |
| Arrow keys / Home / End | Step / jump to min/max (when focused) |
| Hover or drag | Value tooltip |

**Selectors** (multi-position displays like *FF/FB*, *Bell*, *Digital/Tape*)

- **Left-click** cycles forward, **right-click** cycles back, wheel steps.

**Switches** (bat-handle toggles like band *ON*, *Bypass*)

- Click, or press **Space / Enter** when focused.

---

## 6. Per-slot bypass & mix

Each slot has its own **Bypass** and **Mix (wet/dry)** independent of the module:

- **Bypass** — the bat switch on the faceplate. Bypassing crossfades click-free.
- **Mix** — blends the processed (wet) signal against the untouched (dry) signal
  for that slot, for parallel-style processing on any module.

Both are host-automatable parameters (`slotN.bypass`, `slotN.mix`).

---

## 7. A/B compare

Two full snapshots of every parameter, sharing one rack layout, let you compare
two settings:

- **A / B** — switch banks. Your live edits are stashed into the current bank
  when you leave it, and the other bank is recalled.
- **COPY →** — copy the current bank onto the other, so A and B start identical;
  then tweak one and flip back and forth.

Both banks are saved with your preset / project.

---

## 8. MIDI learn

Map a hardware knob or fader (any MIDI **CC**) to almost any control:

1. Connect a MIDI controller (standalone: **Options ▸ Audio/MIDI Settings**;
   plug-in: route MIDI to the AudioRack track).
2. **Right-click** a knob or switch ▸ **MIDI Learn**. An amber ring shows it's
   armed.
3. Move a control on your MIDI device. The next CC binds to it, and a **CCn**
   badge appears.

Move that CC and the parameter follows in real time. To change or remove a
mapping, right-click again ▸ **Forget MIDI CC n**. One parameter maps to one CC;
mappings are saved with your state.

> Selectors (multi-position displays) are excluded from MIDI learn, because
> right-click there already steps their value.

---

## 9. Presets, saving & crash recovery

- **Plug-in:** your rack, all parameters, and both A/B banks are saved inside the
  DAW project and restored with it. Use the DAW's own preset system to store
  named presets.
- **Standalone:** state persists between launches automatically. It also
  **autosaves continuously**: if the app ever quits unexpectedly, the next launch
  restores your rack exactly as it was — you won't lose work.

State is versioned, so presets keep loading in future updates.

---

## 10. Module reference

Categories, faceplate names, and rack heights:

| Module | Name | Cat. | U |
|---|---|---|---|
| [Gain](#gain--gn-1-1u) | GN-1 | Utility | 1U |
| [Compressor](#compressor--cmp-2-2u) | CMP-2 | Dynamics | 2U |
| [Gate](#gate--gt-1-1u) | GT-1 | Dynamics | 1U |
| [Limiter](#limiter--lm-1-1u) | LM-1 | Dynamics | 1U |
| [Parametric EQ](#parametric-eq--eq-6-3u) | EQ-6 | EQ | 3U |
| [Saturator](#saturator--sat-1-1u) | SAT-1 | Tone | 1U |
| [Delay](#delay--dl-2-2u) | DL-2 | Time | 2U |
| [Reverb](#reverb--rv-8-3u) | RV-8 | Time | 3U |

*(Algorithm details for each module live in [docs/modules/](modules/).)*

### Gain — GN-1 (1U)

Clean level trim with a backlit VU meter and stereo LED ladder.

| Control | Range | Notes |
|---|---|---|
| Gain | −60 … +12 dB | Output level (default 0 dB) |

### Compressor — CMP-2 (2U)

Full-featured dynamics with a gain-reduction VU meter.

| Control | Range | Notes |
|---|---|---|
| Threshold | −60 … 0 dB | Level where compression starts |
| Ratio | 1:1 … 20:1 | Amount of reduction above threshold |
| Knee | 0 … 24 dB | Soft-knee width around the threshold |
| Attack | 0.05 … 100 ms | How fast it clamps |
| Release | 5 … 2000 ms | How fast it recovers |
| Makeup | 0 … 24 dB | Manual output make-up gain |
| Auto MK | Manual / Auto | Automatic make-up gain |
| FF / FB | Feed-Forward / Feed-Back | Detector topology |
| PK / RMS | Peak / RMS | Detection mode |
| Prog Rel | Fixed / Prog | Programme-dependent release |
| HPF | 20 … 500 Hz | Sidechain high-pass (detector only) |
| Int / Ext | Internal / External | Sidechain source (Ext uses the sidechain bus) |

### Gate — GT-1 (1U)

Noise gate / expander with hysteresis and an OPEN status LED.

| Control | Range | Notes |
|---|---|---|
| Threshold | −80 … 0 dB | Level where the gate opens |
| Hysteresis | 0 … 24 dB | Gap between open and close thresholds (anti-chatter) |
| Attack | 0.01 … 50 ms | Open speed |
| Hold | 0 … 500 ms | Minimum time held open |
| Release | 5 … 4000 ms | Close speed |
| Range | 0 … 90 dB | Attenuation when closed |
| HPF | 20 … 2000 Hz | Sidechain high-pass |
| Int / Ext | Internal / External | Sidechain source |
| Lookahead | 0 … 10 ms | Opens ahead of transients (adds latency) |

### Limiter — LM-1 (1U)

True-peak brickwall limiter with gain-reduction LEDs and a TP indicator.

| Control | Range | Notes |
|---|---|---|
| Ceiling | −20 … 0 dBTP | True-peak output ceiling (never exceeded) |
| Release | 1 … 1000 ms | Recovery time |
| Lookahead | 0.5 … 10 ms | Detection lookahead (adds latency; reported to the host) |

### Parametric EQ — EQ-6 (3U)

Six-band parametric EQ with a live magnitude curve. Drag a numbered node on the
curve to set that band's **frequency** and **gain**; the band strips give the
type, precise knobs, and on/off.

Per band (×6):

| Control | Range | Notes |
|---|---|---|
| Type | Bell / Low Shelf / High Shelf / High Pass / Low Pass | Filter shape |
| Freq | 20 … 20 000 Hz | Centre / corner frequency |
| Gain | −18 … +18 dB | Boost / cut (bell & shelf types) |
| Q | 0.1 … 10 | Bandwidth / resonance |
| On | Off / On | Enable the band |

Plus a global **Trim** (−12 … +12 dB) output level on the right.

### Saturator — SAT-1 (1U)

Oversampled harmonic saturation with three voicings.

| Control | Range | Notes |
|---|---|---|
| Drive | 0 … 36 dB | Input drive into the curve |
| Character | Tube / Tape / Transistor | Saturation voicing |
| Bias | −1 … +1 | Asymmetry (adds even harmonics) |
| Output | −24 … +12 dB | Output level |
| Mix | 0 … 100 % | Parallel dry/wet blend |
| Auto | Off / On | Drive-compensated output level |

### Delay — DL-2 (2U)

Digital and tape-style delay with host sync and modulation.

| Control | Range | Notes |
|---|---|---|
| Time | 1 … 4000 ms | Delay time (when free) |
| Free / Sync | Free / Sync | Follow the host tempo |
| Division | 1/16 … 1/1 (incl. dotted & triplet) | Note value when synced |
| Feedback | 0 … 100 % | Repeats |
| Mix | 0 … 100 % | Dry/wet |
| Digital / Tape | Digital / Tape | Clean vs. tape colouration |
| Ping-pong | Off / On | Bounce repeats L↔R |
| Tone | 500 … 18 000 Hz | Feedback-path tone (low-pass) |
| Flutter | 0 … 100 % | Wow & flutter depth |
| Offset | −50 … +50 ms | Stereo time offset |

### Reverb — RV-8 (3U)

Eight-line feedback-delay-network reverb.

| Control | Range | Notes |
|---|---|---|
| Size | 0 … 100 % | Room size |
| Decay | 0 … 100 % | Tail length |
| Damping | 0 … 100 % | High-frequency absorption |
| Pre-delay | 0 … 200 ms | Gap before the tail |
| Mix | 0 … 100 % | Dry/wet |
| Width | 0 … 100 % | Stereo spread |
| Freeze | Off / Freeze | Infinite-sustain hold |

---

## 11. Keyboard & mouse quick reference

| Input | Action |
|---|---|
| Drag knob | Adjust · **Shift** = fine |
| Double-click knob | Reset to default |
| Wheel on knob/selector | Step · **Shift** = fine (knobs) |
| Left / right-click selector | Cycle forward / back |
| Click switch · Space / Enter | Toggle |
| Right-click knob or switch | Context menu (MIDI Learn) |
| Right-click unit | Bypass / Remove / Duplicate |
| Drag rack ears | Move / swap a unit |
| Drag unit → browser | Remove |
| Double-click empty slot | Mount a module |

---

## 12. Troubleshooting

**Standalone: no sound / meters don't move (even in Audio Settings).**
The input stream isn't running at the device level. Check, in order:
1. **Microphone permission** was granted (macOS: System Settings ▸ Privacy &
   Security ▸ Microphone).
2. **Mute audio input** is unchecked in Options ▸ Audio Settings.
3. Input and output devices **share a clock** — avoid a built-in mic paired with
   a Bluetooth output (see the tip in [First launch](#2-first-launch)). Use
   built-in in+out, or one audio interface.

**Plug-in doesn't appear in my DAW.** Rescan plug-ins (or restart the DAW).
Confirm the format installed to the standard folder (see [Installing](#1-installing))
and that your DAW scans it. On Windows, a first-run DAW may need the WebView2
runtime that the installer provides.

**The interface is blank (Windows).** Install the Microsoft Edge WebView2 runtime
— the AudioRack installer does this automatically; if you copied files manually,
install it from Microsoft.

**A limiter or gate adds latency.** That's the honest lookahead being reported to
the host; the DAW compensates for it. Reduce or zero the Lookahead control to
remove it.
