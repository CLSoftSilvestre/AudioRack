# Gate — GT-1 (Dynamics, 1U)

Hysteresis noise gate with hold, range, sidechain filtering and lookahead
(Reiss & McPherson, *Audio Effects: Theory, Implementation and Application*,
ch. 6).

## Design

- **Detection**: mono sidechain (internal or external bus) → RBJ HPF
  (20 Hz–2 kHz) → |x| → fast envelope (0.05 ms attack / 5 ms release), so the
  state machine reacts to transients immediately without chattering on
  waveform ripple.
- **State machine**: `closed → open` when env > threshold; `open → holding`
  when env < threshold − hysteresis (the window prevents chatter);
  `holding → closed` after the hold time unless the envelope re-opens it.
- **Gain**: one-pole toward 1.0 (attack τ) or toward `10^(−range/20)`
  (release τ).
- **Lookahead** delays the *audio only*; the undelayed detector therefore
  leads the programme, and the gate is already opening when the transient
  arrives. The delay is reported as latency.

## Parameters

| ID suffix | Range | Default |
|---|---|---|
| `threshold` | −80…0 dB | −40 |
| `hysteresis` | 0…24 dB | 3 |
| `attack` | 0.01…50 ms | 0.5 |
| `hold` | 0…500 ms | 50 |
| `release` | 5…4000 ms | 150 |
| `range` | 0…90 dB | 80 |
| `schpf` | 20 Hz…2 kHz | 20 |
| `scsource` | Internal/External | Internal |
| `lookahead` | 0…10 ms | 0 |

## Verified by tests

Below-threshold attenuation ≥ range−10 dB; opens within 10 ms on programme;
hold window passes a −50 dB probe, two release constants later it is ≥ 20 dB
down; a −26 dB signal between the open (−20) and close (−32) thresholds keeps
the gate open (no chatter); 5 ms lookahead preserves ≥ 1.5× more of a
single-cycle transient than zero lookahead.
