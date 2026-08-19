# ADR 0009 — Real-time analyser: a shared component, not an analyser module

Status: accepted (2026-08-19)

## Context

EQ-6 draws a magnitude-response curve but nothing about the signal, so the user
is aiming filters blind. The obvious ways to fix that pull in opposite
directions:

1. Put an FFT inside `EqModule` and draw it behind the curve.
2. Build the §5 Meter/Analyzer module and let the user place an RTA wherever
   they want one.

Option 1 is what people actually want from an EQ, but done privately it
duplicates the analyser DSP and a whole transport channel the moment the
Meter/Analyzer module lands. Option 2 delays the thing that makes EQ-6 usable
and still leaves the EQ blind.

The expensive part of either is not the faceplate. It is the analyser DSP plus
getting ~96 bands per module from the audio thread to the WebView at UI rate —
`MeterFrame` is five floats and `ar_meters` is built around that shape, so it
cannot carry a spectrum.

## Decision

Build the analyser once as `dsp::SpectrumAnalyser` (header-only, in
`src/dsp/common/`) and give `AudioModule` an optional `readSpectrum()` hook.
EQ-6 is the first consumer; the Meter/Analyzer module becomes a faceplate over
DSP that already exists and is already tested.

Three consequences worth recording:

**The FFT runs on the message thread.** `push()` on the audio thread mono-sums
the block into a plain circular buffer and publishes the write position with a
release store — measured at 0.48 µs per 512-sample block, 0.01 % of the
realtime budget. `render()` does the window, the 4096-point FFT and the band
fold on the message thread, at 21.9 µs per call. Doing the FFT on the audio
thread would have been realtime-*safe* (no allocation with a preallocated
`juce::dsp::FFT`) but it would have spent 2 % of the block budget per EQ on work
that exists only to draw a picture.

The ring is 4× the FFT window, so the writer must run more than three windows
(~256 ms at 48 kHz) ahead of a reader mid-copy to tear a frame. A UI poll that
late has already dropped many frames; the failure mode is one torn display
frame, never a stall or a crash.

**Modules are read from the message-thread mirror.** `pumpSpectra()` calls
`RackEngine::mountedModule()`, which is only mutated on the message thread, and
displaced modules are destroyed by `collectGarbage()` on that same thread after
the mirror stops naming them. So the editor can call a method on a live module
without a lock, while the audio thread is inside `process()` on it.

**Bands are geometric, and edges stay fractional.** 96 bands from 20 Hz to
22 kHz. At 48 kHz with a 4096-point FFT the bins are 11.7 Hz apart, so every
band below ~450 Hz is narrower than one bin. Snapping edges to integer bins and
forcing them apart — the obvious implementation, and the first one written here
— marches the low bands steadily up in frequency: band 22 landed on 281 Hz
instead of 100 Hz, misaligning the bottom third of the display against the EQ
curve drawn over it. Edges are therefore kept as fractional bins and sub-bin
bands are linearly interpolated, which also recovers most of the Hann scalloping
loss. `tests/SpectrumTests.cpp` pins 100 Hz, 200 Hz and 5 kHz to ±1.5 dB
specifically to keep that regression from coming back.

## Alternatives rejected

- **Widening `MeterFrame`.** Meters are folded per slot at 60 Hz with a
  max-fold on peaks; a spectrum wants neither the fold nor the rate. A separate
  `ar_spectrum` event at 30 Hz, on the timer tick opposite the parameter flush,
  keeps both simple.
- **Sending raw float bands.** JSON floats are ~3× the bytes of the rounded
  half-dB integers actually sent, for resolution finer than a display pixel.
- **Pre-EQ tap, or a switchable pre/post.** Post-EQ means the curve and the
  spectrum describe the same signal, which is the point of drawing them on one
  canvas. A PRE/POST switch needs a parameter and a second analyser instance;
  it is worth revisiting when the Meter/Analyzer module gives the RTA a home of
  its own.
- **An RTA on/off parameter.** Skipped for now: the analyser costs the audio
  thread 0.01 % of a block and the message thread 1.3 % of one core at twelve
  simultaneous EQs, so there is nothing to switch off yet.
