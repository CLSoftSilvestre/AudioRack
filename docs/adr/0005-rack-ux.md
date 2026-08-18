# ADR 0005 — Rack UX: drag & drop, and A/B compare (M7a)

Date: 2026-08-18 · Status: accepted

M7 (Rack UX) is split into two reviewable commits. This is **M7a**: the module
browser, drag & drop / reorder / remove, duplicate, and A/B compare. **M7b**
(next) adds MIDI learn, which is a different kind of change — it enables a MIDI
input on a currently MIDI-free effect and adds an audio-thread CC path.

## 1. Pointer-based drag & drop, not HTML5 DnD

JUCE's `WebBrowserComponent` is WKWebView (macOS) / WebView2 (Windows). Native
HTML5 drag-and-drop is unreliable inside embedded webviews (drag images, drop
events and `dataTransfer` behave inconsistently, and WKWebView historically
disables intra-page content dragging). So all dragging is driven from **pointer
events** (`dnd.ts`): press → threshold → ghost element that follows the pointer
→ drop. This also gives a photoreal drag "ghost" and precise drop highlighting
for free, and it is trivially testable in a plain browser.

Hit-testing uses `document.elementsFromPoint`, which works in client
coordinates and is therefore immune to the CSS `scale()` transform `main.ts`
applies to the stage — no manual rect maths, no reflow on drag.

The **drag handle is the rack ears**, matching real hardware (you grab a unit by
its rack ears). Handles are wired per-mount in `RackFrame`, which knows the slot
number; `unitKit` stays layout-only. Because the ears are separate elements from
the faceplate widgets, unit-drag never conflicts with knob/fader pointer drags.

Drop rules: a **new** module drops only onto an empty slot; a **move** swaps with
whatever occupies the target (reusing the engine's existing swap semantics), or
removes the unit when dropped on the browser panel. Invalid targets flash red,
valid ones green, via a `::after` overlay that stacks above the mounted unit.

## 2. A/B compare stores parameters only; the rack is shared

Per the product decision, A and B capture **parameter values only** — the rack
layout (which modules sit in which slots) is shared between banks. This keeps
A/B a click-free morph of knob/switch values and never triggers structural
(audio-thread) add/remove churn on a flip.

Only the **inactive** bank is stored explicitly. The active bank is, by
definition, the live APVTS state; it is captured lazily into its slot the moment
you leave it (`selectBank`) and synced once more in `getStateInformation` before
serialisation. `copyBankToOther` makes the two equal so you can audition two
variations of the same starting point. All of this is message-thread APVTS glue
(`setValueNotifyingHost`); there is no audio-thread code and no new DSP.

Banks are persisted in the state JSON under an additive, optional `"ab"` node
(schema stays v1): `{ active, a: {id→value01}, b: {...} }`. Presets written
before this change simply lack the node and seed both banks from their loaded
values on load. Readers ignore unknown fields, so old and new plugin versions
still read each other's presets.

## 3. Why no new Catch2 test for M7a

The A/B and duplicate logic lives on `AudioRackProcessor` and depends on the
APVTS and the plugin's generated `JucePlugin_*` config, which the DSP test target
deliberately does not link (it pulls in only `juce_dsp`, no plugin client). M7a
introduces **no DSP and no audio-thread code**, so there is no numerical claim to
lock with a unit test. It is verified instead by: a clean `tsc` type-check + Vite
build, `pluginval --strictness-level 10` (its parameter/state fuzz round-trips
the new `"ab"` state; its Editor + Editor-Automation tests instantiate the new
layout), and browser-preview screenshots driving the mock bridge. The 37 DSP
tests remain green and unchanged.

## 4. Layout: fixed browser column beside a scaled stage

The studio is now `[ browser (fixed 210px) | scrollable stage ]`. The stage
(top toolbar + rack) is still authored at 1060 logical px and scaled to its
column's width, so faceplate geometry stays proportional; the browser panel
sits outside the scaled stage at native size for crisp text. The default editor
size grew to 1300×780 to fit the panel plus a full-width rack.
