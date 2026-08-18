# Saturator — SAT-1 (Tone, 1U)

Waveshaping saturator (Zölzer, "DAFX" ch. 5; Pirkle, "Designing Audio Effect
Plugins in C++", nonlinear processing).

## Design

`drive → bias → waveshaper → DC block → drive-compensated output`, with the
shaping stage running **4× oversampled** through a JUCE polyphase
half-band-FIR oversampler. Harmonics generated above fs/2 by the
nonlinearity are created in the oversampled domain and filtered before
decimation, keeping aliasing far down. The oversampler's FIR delay is
reported as latency.

Curves:

- **Tube** — asymmetric soft clip; the positive and negative halves saturate
  differently, producing the characteristic even-harmonic warmth. Bias shifts
  the operating point. A DC blocker removes the offset the asymmetry adds.
- **Tape** — symmetric `tanh`-family curve with gentle extreme-flattening;
  mostly odd harmonics, soft knee.
- **Transistor** — harder cubic soft-clip; sharper knee, more edge.

**Auto-gain** divides out each curve's small-signal gain at the current drive,
so more drive means more harmonics, not just more level. **Mix** blends the
shaped signal with the clean input for parallel saturation.

## Parameters

`drive` (0–36 dB), `type` (Tube/Tape/Transistor), `bias` (±1),
`out` (−24…+12 dB), `mix`, `autogain` (Off/On).

## Verified by tests

A pure 1 kHz sine acquires measurable odd-harmonic content (3rd/fundamental
> 1%); auto-gain keeps output within ~8 dB across a 21 dB drive change;
latency is reported > 0 (the oversampler delay).
