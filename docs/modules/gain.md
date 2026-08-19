# Gain (Utility, 1U)

The utility module: a smoothed gain plus channel conditioning, used to prove the
engine, parameter and UI paths end-to-end.

## Algorithm

**Channel mode** is applied first, before the gain. For a stereo block it
optionally collapses the two channels to a mono signal duplicated across L and R:

- **Stereo** — passthrough (default).
- **Mono** — `0.5·(L+R)` to both (−6 dB sum, so a centred stereo source keeps
  its level).
- **Left** — `L` to both (a mono source on input 1 — e.g. a guitar — at full
  level).
- **Right** — `R` to both (a mono source on input 2).

Then the gain:

`y[n] = g[n] · x[n]`, where `g` ramps linearly over 10 ms towards
`10^(dB/20)`. The ramp (a `juce::SmoothedValue`) removes zipper noise on
parameter jumps; 10 ms is short enough to feel immediate, long enough that a
full −60→+12 dB sweep produces no audible step.

At −60 dB the gain snaps to exactly 0 (true mute) via
`juce::Decibels::decibelsToGain`'s minus-infinity floor.

Metering is post-gain block peak and block RMS per channel, published through
relaxed atomics; the UI applies its own ballistics.

## Parameters

| ID suffix | Name | Range | Default | Notes |
|---|---|---|---|---|
| `gaindb` | Gain | −60 … +12 dB | 0 dB | skewed so 0 dB sits at the knob's centre; −60 = −∞ |
| `chmode` | Channels | Stereo / Mono / Left / Right | Stereo | channel conditioning, applied before the gain |

Latency: 0 samples.
