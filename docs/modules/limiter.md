# Limiter — LM-1 (Dynamics, 1U)

Lookahead brickwall limiter with true-peak-aware detection.

## Design

The gain chain guarantees *no overshoot by construction* (sliding-minimum +
bounded-support smoothing, cf. the "perfect limiter" construction popularised
by Signalsmith Audio):

1. **Detector** `d[n]`: per-sample max of |L|, |R| and their 4× oversampled
   interpolation (JUCE polyphase half-band FIR, integer latency δ). This is
   what makes the ceiling inter-sample-peak aware.
2. **Target gain** `t[n] = min(1, c/d[n])` with `c` the linear ceiling.
3. **Release**: falls instantly, rises via one-pole with the release τ.
4. **Sliding minimum** over the last `L+1` samples (Lemire monotonic wedge).
5. **Smoothing**: two cascaded boxcars of `⌊L/2⌋+1` samples. Their total
   support (`2⌊L/2⌋+1 ≤ L+1`) never exceeds the min window, so the smoothed
   gain cannot exceed any requirement inside the window — the delayed sample
   always receives a gain ≤ its own target.
6. **Output**: programme delayed by `L+δ`, multiplied, then hard-clamped at
   `c` as a final guard (the tests show the clamp is not doing the limiting).

**Latency** = `L + δ`, recomputed when the lookahead changes and reported to
the host through the engine (`RackEngine` sums per-module latencies).

Residual true-peak error with 4× detection is < ~0.3 dB worst-case; for
ISP-critical delivery set the ceiling ≤ −0.3 dBTP (the default).

## Parameters

| ID suffix | Range | Default |
|---|---|---|
| `ceiling` | −20…0 dBTP | −0.3 |
| `release` | 1…1000 ms | 100 |
| `lookahead` | 0.5…10 ms | 2 |

## Verified by tests

Impulse-measured latency exactly equals `latencySamples()`; latency tracks
the lookahead parameter; −12 dB programme nulls against the delayed input to
< 1e-4; a +6 dB hostile programme (hot sine, ±1.9 spikes, square bursts)
never exceeds the ceiling by more than 0.0001×.
