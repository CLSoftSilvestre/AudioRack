# ADR 0004 — Time & tone module design choices (M6)

Date: 2026-08-18 · Status: accepted

## 1. Fractional delay: Lagrange, not allpass

Delay time is modulated (tape wow/flutter, reverb line modulation) and
automated. Allpass fractional interpolation has recursive state that produces
transients when the delay moves; 4-point Lagrange is stateless and glitch-free
under modulation, at the cost of slight HF roll-off — inaudible for these uses.
One `FractionalDelay` serves delay and reverb.

## 2. Reverb: Hadamard FDN over Dattorro plate

The brief offered "FDN (8×8 Hadamard) or Dattorro plate". FDN chosen: the
Hadamard mixing matrix is provably lossless (orthogonal), so stability and the
freeze feature (`g = 1`) are trivially correct, and the 8-point transform is a
24-op butterfly. Dattorro's plate is a specific topology with hand-tuned
allpass constants — more character but less general and harder to make
freeze/size-scale cleanly. FDN also reuses `FractionalDelay` for line
modulation.

## 3. Saturator: detection-domain vs full oversampling

Unlike the limiter (detector-only oversampling), the saturator oversamples the
**actual signal path** 4× — the nonlinearity *is* the audio, so its alias
products must be band-limited before decimation. Reported latency is the
oversampler FIR delay. Curves are closed-form (`tanh`, cubic, asymmetric soft
clip) rather than lookup tables: cheap enough at 4× and exact.

## 4. EQ: shared RBJ formulas across the language boundary

Rather than stream a computed curve from C++ to the UI, the TypeScript EQ
display re-implements the exact RBJ biquad equations. Both sides are ~40 lines
and the response is deterministic from the six per-band values, so duplication
is cheaper and lower-latency than a curve-data channel, and the two are
trivially cross-checkable. If they ever diverge it is a visible bug, not silent
drift.

## 5. Browser-preview parameter schema

`ui/src/bridge/previewSchema.ts` gives the mock bridge realistic defaults and
display-text formatting for every module, so `npm run dev` shows faithful
faceplates without the plugin. It is dev-only: when `window.__JUCE__` exists
the native backend is the sole source of parameter values and text.
