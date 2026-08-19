# Guitar Amp — AMP-1 (Amp, 3U)

A guitar amplifier: cascaded tube preamp → passive-style tone stack →
power-amp saturation → switchable speaker cabinet.

References: Yeh, Abel & Smith, "Simulation of the diode limiter in guitar
distortion circuits…" (DAFx 2007); Yeh & Smith, "Discretization of the '59
Fender Bassman tone stack" (DAFx 2006); Kuehnel, "Guitar Amplifier Circuit
Analysis"; J.O. Smith, "Physical Audio Signal Processing" (speaker cabinet as a
resonant, band-limited system).

## Design

Per channel:

```
input gain → [N cascaded tube stages: asymmetric shaper → coupling HP →
  bright LP → interstage gain] → tone stack (bass/mid/treble) → presence
  → power-amp saturation                        ← all 4× oversampled
→ cabinet (HP → box resonance → scoop → cone peak → LP → comb)
→ DC block → master / output                    ← base rate
```

The nonlinear preamp and power stages run **4× oversampled** through a JUCE
polyphase half-band-FIR oversampler, so the harmonics the nonlinearities
synthesise above fs/2 are created in the oversampled domain and filtered before
decimation. The tone stack sits **inside** the oversampled region, between
preamp and power amp — exactly where it is in the real circuit. The oversampler's
FIR delay is reported as the module's latency.

### Preamp

Each gain stage is an **asymmetric triode shaper** (positive half saturates
sooner than the negative half → even-harmonic warmth on top of the odd
harmonics), followed by a **coupling highpass** (the interstage coupling
capacitor: blocks the DC the asymmetry introduces and tightens the low end) and
a **bright lowpass** (grid-stopper / Miller roll-off). Stages cascade through a
fixed interstage gain.

The **Channel** control selects how many stages the signal runs through and how
hard it is driven:

- **Clean** — one nearly-linear stage.
- **Crunch** — two stages into mild clip.
- **Lead** — three stages, the last two hard into saturation.

Each channel carries a level trim so the three voicings sit at roughly matched
loudness.

### Tone stack

**Bass** (low shelf ~100 Hz), **Middle** (peak ~500 Hz) and **Treble** (high
shelf ~2.5 kHz), each ±12 dB with a flat centre at 5. **Presence** is a
boost-only high shelf (~3.5 kHz) modelling the power-amp negative-feedback
presence control. This is a shelf/peak approximation of the passive tone
network; the full interacting model (Yeh & Smith 2006) would add the network's
inherent mid scoop.

### Cabinet (analytic, no impulse files)

Each cabinet is a small chain of biquads whose corners and resonances match the
measured behaviour of 12" speaker enclosures — there are **no impulse-response
files**. The chain is: sub-resonance highpass → low-frequency box resonance →
low-mid scoop → cone break-up presence peak → top-end roll-off → a short
first-reflection comb (mic-position colouration).

- **1x12** — open-back combo: tight, bright, light low end (resonance ~100 Hz,
  roll-off ~5.2 kHz).
- **2x12** — fuller, a touch darker (resonance ~88 Hz, roll-off ~4.8 kHz).
- **4x12** — closed-back stack: big low resonance, scooped low-mids, dark top
  (resonance ~78 Hz, roll-off ~4.2 kHz).

The cabinet is linear, so it runs once at the base sample rate after decimation.

## Parameters

`channel` (Clean/Crunch/Lead), `gain`, `bass`, `mid`, `treble`, `presence`,
`master` (all 0–10 amp dials), `cab` (1x12/2x12/4x12).

## Verified by tests

The cascaded preamp adds harmonics to a pure tone, and the amount rises with the
channel (Clean < Crunch < Lead); the treble and bass controls move the spectral
balance in their bands; each cabinet band-limits the top end, with a bigger box
measurably darker (1x12 > 2x12 > 4x12 high-frequency energy); output stays finite
and bounded at extreme settings; latency is reported > 0 (the oversampler delay).
