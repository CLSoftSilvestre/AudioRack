# Delay — DL-2 (Time, 2U)

Stereo delay with digital and tape modes.

## Design

- **Fractional delay** via 4-point (3rd-order) Lagrange interpolation (Laakso
  et al., "Splitting the Unit Delay", 1996). Lagrange has no recursive state,
  so the modulated read used by tape mode and by time automation is
  glitch-free — no zipper noise, no allpass transients.
- **Digital mode**: clean repeats; feedback path has an optional tone LPF.
- **Tape mode**: the read position is modulated by a wow (~0.6 Hz) + flutter
  (~6.3 Hz) LFO, and the feedback path is always tone-filtered (head loss).
  Feedback is soft-clipped (`tanh`) so self-oscillation saturates instead of
  blowing up.
- **Host sync**: time becomes a note division of the transport tempo
  (`samples = 60/bpm · beats · fs`); divisions from 1/16 to 1/1 incl. dotted
  and triplet.
- **Ping-pong**: each channel's feedback is injected into the opposite line.
- **Stereo offset**: ±50 ms added to the right channel for width/slapback.

Time changes are 20 ms-smoothed and the Lagrange reader resolves the
sub-sample motion.

## Parameters

`time` (1–4000 ms), `sync` (Free/Sync), `division`, `feedback` (0–110%),
`mix`, `mode` (Digital/Tape), `pingpong`, `tone` (500 Hz–18 kHz),
`flutter` (wow/flutter depth), `offset` (±50 ms).

## Verified by tests

Impulse appears at exactly the set delay (100 ms → sample 4800); host-sync
1/4 at 120 bpm lands at 24000 samples; feedback produces decaying repeats
(2nd < 1st, both present).
