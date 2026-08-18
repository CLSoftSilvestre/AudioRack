# Gain (Utility, 1U)

The simplest possible module, used to prove the engine, parameter and UI
paths end-to-end.

## Algorithm

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

Latency: 0 samples.
