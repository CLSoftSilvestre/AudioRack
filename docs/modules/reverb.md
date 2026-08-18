# Reverb — RV-8 (Time, 3U)

Feedback Delay Network reverb (Jot & Chaigne, "Digital delay networks for
designing artificial reverberators", AES 1991; Välimäki et al., "Fifty Years
of Artificial Reverberation", IEEE TASLP 2012).

## Design

Eight delay lines with mutually non-commensurate lengths are mixed each sample
by a normalised 8×8 Hadamard matrix — an orthogonal (lossless) rotation, so
the recursion neither gains nor loses energy at feedback gain `g = 1`. The
global gain `g` (from the Decay control) sets the tail length; a per-line
damping low-pass sets how fast highs decay relative to lows.

- **Hadamard** applied by a fast 8-point butterfly (24 add/sub, not 64 MACs).
- **Line modulation**: each length is slowly modulated (~0.5 Hz) so the modal
  response never rings on a single pitch.
- **Predelay** 0–200 ms before the network; **Width** blends the stereo output
  toward mono; **Size** scales the line lengths.
- **Freeze**: `g = 1` for an infinite tail; input keeps flowing so you can
  layer into the frozen wash.

Output taps: even lines → L, odd → R for stereo decorrelation.

## Parameters

`size`, `decay`, `damping`, `predelay` (0–200 ms), `mix`, `width`,
`freeze` (Off/Freeze).

## Verified by tests

A 50 ms burst leaves a tail that is present and decaying after it stops;
freeze sustains the tail (energy at 2.5 s ≥ half the energy at 1 s) and stays
finite.
