# Parametric EQ — EQ-6 (EQ, 3U)

Six-band parametric equaliser. All coefficients are RBJ Audio-EQ-Cookbook
biquads (Robert Bristow-Johnson); the UI draws the magnitude response from the
identical formulas so what you see is what the DSP does.

## Per band

- **Type**: Bell (peaking), Low Shelf, High Shelf, High Pass, Low Pass.
- **Freq** 20 Hz–20 kHz (log-skewed), **Gain** ±18 dB (bell/shelf only),
  **Q** 0.1–10.
- **On** and **Solo**. When any band is soloed, only soloed+enabled bands are
  in the chain (audition "what this band is doing").

Output **Trim** ±12 dB, smoothed.

Processing is Direct-Form-II transposed, per channel; coefficients recompute
only when a band value changes, so a static EQ is six biquads per channel and
no per-sample transcendentals.

## UI

The response display computes `|H(e^jω)|` for each active band and sums the log
magnitudes across a log frequency axis; band handles are draggable (X = freq,
Y = gain) and drive the parameters directly. The C++ and TypeScript coefficient
code are line-for-line the same RBJ equations.

### Real-time analyser

Behind the curve sits a live spectrum of the module's **own output** — post-EQ,
before the slot's wet/dry crossfade — so the curve and the spectrum always
describe the same signal. It comes from the shared
`dsp::SpectrumAnalyser` (`src/dsp/common/SpectrumAnalyser.h`): 4096-point Hann
periodogram, 96 geometric bands from 20 Hz to 22 kHz, fast attack and slow
release, calibrated so a full-scale sine reads 0 dBFS in its band.

The FFT does **not** run on the audio thread. `process()` only mono-sums the
block into a lock-free ring (0.48 µs per 512-sample block); the editor's 30 Hz
timer calls `readSpectrum()`, which does the window, transform and band fold on
the message thread (21.9 µs per call) and ships the bands as rounded half-dB
integers over the `ar_spectrum` event. See ADR 0009.

Two axes share the canvas on purpose: the response curve reads ±18 dB of
*gain*, the analyser reads 0…−72 dBFS of *level*. The analyser is drawn dim and
unlabelled — it is context for the curve, not a second copy of it.

## Verified by tests

`tests/TimeToneTests.cpp` — a +12 dB bell at 1 kHz measures +12 dB ±1.5 at
centre and ~0 dB two decades away; a 500 Hz highpass is >10 dB down at 100 Hz
and flat at 5 kHz; soloing a flat band removes a boosting band from the measured
response.

`tests/SpectrumTests.cpp` — a full-scale 1 kHz sine reads 0 dBFS ±1 in its own
band and the peak lands in that band; level tracks amplitude in dB; 100 Hz and
5 kHz tones resolve into separate bands with >30 dB of clear air between them;
the display decays to the floor when the transport stops instead of freezing;
and a −18 dB notch at 1 kHz is visible in EQ-6's spectrum, which is what proves
the tap is after the filters rather than before them.
