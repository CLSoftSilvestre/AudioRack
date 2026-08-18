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

## Verified by tests (tests/TimeToneTests.cpp)

A +12 dB bell at 1 kHz measures +12 dB ±1.5 at centre and ~0 dB two decades
away; a 500 Hz highpass is >10 dB down at 100 Hz and flat at 5 kHz; soloing a
flat band removes a boosting band from the measured response.
