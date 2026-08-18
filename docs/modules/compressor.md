# Compressor — CMP-2 (Dynamics, 2U)

Reference: Giannoulis, Massberg & Reiss, *Digital Dynamic Range Compressor
Design — A Tutorial and Analysis*, JAES 60(6), 2012.

## Signal path

Detector source → sidechain HPF → level (dB) → gain computer → decoupled
smoothing → gain + makeup applied to the programme.

- **Detector source**: mono mix of the main input (feed-forward), the previous
  output sample (feed-back), or the external sidechain bus. External source
  takes precedence over topology when the bus is active.
- **Sidechain HPF**: RBJ 2nd-order highpass, 20–500 Hz, Q = 0.707. At 20 Hz it
  is effectively transparent.
- **Level**: peak = instantaneous `20·log10|sc|`; RMS = 5 ms one-pole average
  of `sc²`, then `10·log10`.
- **Gain computer** (dB in → dB out, threshold `T`, ratio `R`, knee width `W`):
  below the knee unity; inside `|2(L−T)| ≤ W`:
  `L + (1/R − 1)(L − T + W/2)² / 2W`; above: `T + (L−T)/R`.
- **Smoothing** — the paper's *smooth decoupled peak detector*:
  `yR[n] = max(g[n], aR·yR[n−1] + (1−aR)·g[n])`, then
  `gr[n] = aA·gr[n−1] + (1−aA)·yR[n]`, with `a = e^(−1/(τ·fs))`.
- **Program-dependent release**: the release τ is scaled by the ~1 s average
  GR: `τ_eff = τ·clamp(0.25 + grAvg/6, 0.25, 4)` — brief peaks recover about
  4× faster than the dial, sustained squash releases up to 4× slower.
- **Auto-makeup**: `−G(0)/2` — half the reduction a 0 dBFS signal would
  receive. With T = −20, R = 4 that is +7.5 dB.

## Parameters

| ID suffix | Range | Default | Notes |
|---|---|---|---|
| `threshold` | −60…0 dB | −18 | |
| `ratio` | 1…20:1 | 4 | skew centred at 4:1 |
| `knee` | 0…24 dB | 6 | dB-domain soft knee |
| `attack` | 0.05…100 ms | 10 | τ of the attack smoother |
| `release` | 5…2000 ms | 150 | τ of the release stage |
| `makeup` | 0…24 dB | 0 | ignored when Auto |
| `automakeup` | Manual/Auto | Manual | |
| `topology` | FF/FB | FF | FB reaches less GR, softer character |
| `detector` | Peak/RMS | Peak | |
| `pdr` | Fixed/Program | Fixed | program-dependent release |
| `schpf` | 20…500 Hz | 20 | detector-only highpass |
| `scsource` | Internal/External | Internal | external = host sidechain bus |

Latency: 0. GR is published per block via `MeterFrame.gainReductionDb`.

## Verified by tests (tests/DynamicsTests.cpp)

Static curve slope = 1/R ±0.06; knee GR at threshold = 1.125 dB ±0.6;
attack/release time constants within [0.4×, 3×] of dialled values; external
sidechain and SC HPF behaviour; auto-makeup level; FB stability.
