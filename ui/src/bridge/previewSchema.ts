/** Browser-preview only: default values and display-text formatting for every
 *  module parameter, so `npm run dev` shows realistic faceplates without the
 *  plugin. The real plugin is the source of truth at runtime; this table is
 *  never consulted when window.__JUCE__ is present.
 */

export interface PreviewParam {
  suffix: string;
  default01: number;
  format: (v01: number) => string;
}

const pct = (v: number) => `${Math.round(v * 100)} %`;
const choice = (labels: string[]) => (v: number) =>
  labels[Math.round(v * (labels.length - 1))] ?? "";

// Skewed range mirror (matches juce NormalisableRange.setSkewForCentre).
function skew(min: number, max: number, centre: number, v01: number): number {
  const s = Math.log(0.5) / Math.log((centre - min) / (max - min));
  return min + (max - min) * Math.pow(v01, 1 / s);
}

const hz = (min: number, max: number, centre: number, unit = "Hz") => (v: number) => {
  const f = skew(min, max, centre, v);
  return f >= 1000 ? `${(f / 1000).toFixed(2)} k${unit}` : `${f.toFixed(0)} ${unit}`;
};
const lin = (min: number, max: number, unit: string, digits = 1) => (v: number) =>
  `${(min + (max - min) * v).toFixed(digits)} ${unit}`;

const eqBand = (i: number): PreviewParam[] => [
  { suffix: `b${i}type`, default01: 0, format: choice(["Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass"]) },
  { suffix: `b${i}freq`, default01: 0.5, format: hz(20, 20000, 632) },
  { suffix: `b${i}gain`, default01: 0.5, format: lin(-18, 18, "dB") },
  { suffix: `b${i}q`, default01: 0.28, format: (v) => skew(0.1, 10, 0.71, v).toFixed(2) },
  { suffix: `b${i}on`, default01: 1, format: choice(["Off", "On"]) },
  { suffix: `b${i}solo`, default01: 0, format: choice(["Off", "Solo"]) },
];

// value01 that lands a skewed freq range on a specific Hz default.
function inv(min: number, max: number, centre: number, target: number): number {
  const s = Math.log(0.5) / Math.log((centre - min) / (max - min));
  return Math.pow((target - min) / (max - min), s);
}

const EQ_DEFAULT_FREQS = [60, 150, 400, 1000, 3500, 10000];

export const PREVIEW_SCHEMA: Record<string, PreviewParam[]> = {
  gain: [
    { suffix: "gaindb", default01: 0.5, format: (v) => `${(v * 72 - 60).toFixed(1)} dB` },
    { suffix: "chmode", default01: 0, format: choice(["Stereo", "Mono", "Left", "Right"]) },
  ],

  comp: [
    { suffix: "threshold", default01: 0.7, format: lin(-60, 0, "dB") },
    { suffix: "ratio", default01: 0.4, format: (v) => `${skew(1, 20, 4, v).toFixed(1)}:1` },
    { suffix: "knee", default01: 0.25, format: lin(0, 24, "dB") },
    { suffix: "attack", default01: 0.3, format: hz(0.05, 100, 10, "ms") },
    { suffix: "release", default01: 0.35, format: hz(5, 2000, 150, "ms") },
    { suffix: "makeup", default01: 0, format: lin(0, 24, "dB") },
    { suffix: "automakeup", default01: 0, format: choice(["Manual", "Auto"]) },
    { suffix: "topology", default01: 0, format: choice(["FF", "FB"]) },
    { suffix: "detector", default01: 0, format: choice(["Peak", "RMS"]) },
    { suffix: "pdr", default01: 0, format: choice(["Fixed", "Prog"]) },
    { suffix: "schpf", default01: 0, format: hz(20, 500, 100) },
    { suffix: "scsource", default01: 0, format: choice(["Int", "Ext"]) },
  ],

  gate: [
    { suffix: "threshold", default01: 0.5, format: lin(-80, 0, "dB") },
    { suffix: "hysteresis", default01: 0.125, format: lin(0, 24, "dB") },
    { suffix: "attack", default01: 0.3, format: hz(0.01, 50, 1, "ms") },
    { suffix: "hold", default01: 0.1, format: lin(0, 500, "ms", 0) },
    { suffix: "release", default01: 0.35, format: hz(5, 4000, 200, "ms") },
    { suffix: "range", default01: 0.89, format: lin(0, 90, "dB", 0) },
    { suffix: "schpf", default01: 0, format: hz(20, 2000, 200) },
    { suffix: "scsource", default01: 0, format: choice(["Int", "Ext"]) },
    { suffix: "lookahead", default01: 0, format: lin(0, 10, "ms") },
  ],

  eq: [
    ...EQ_DEFAULT_FREQS.flatMap((f, i) => {
      const band = eqBand(i);
      band[1].default01 = inv(20, 20000, 632, f); // freq default
      return band;
    }),
    { suffix: "trim", default01: 0.5, format: lin(-12, 12, "dB") },
  ],

  sat: [
    { suffix: "drive", default01: 0.17, format: lin(0, 36, "dB") },
    { suffix: "type", default01: 0, format: choice(["Tube", "Tape", "Transistor"]) },
    { suffix: "bias", default01: 0.5, format: (v) => (v * 2 - 1).toFixed(2) },
    { suffix: "out", default01: 0.67, format: lin(-24, 12, "dB") },
    { suffix: "mix", default01: 1, format: pct },
    { suffix: "autogain", default01: 1, format: choice(["Off", "On"]) },
  ],

  amp: [
    { suffix: "channel", default01: 0, format: choice(["Clean", "Crunch", "Lead"]) },
    { suffix: "gain", default01: 0.5, format: (v) => (v * 10).toFixed(1) },
    { suffix: "bass", default01: 0.5, format: (v) => (v * 10).toFixed(1) },
    { suffix: "mid", default01: 0.5, format: (v) => (v * 10).toFixed(1) },
    { suffix: "treble", default01: 0.5, format: (v) => (v * 10).toFixed(1) },
    { suffix: "presence", default01: 0.5, format: (v) => (v * 10).toFixed(1) },
    { suffix: "master", default01: 0.5, format: (v) => (v * 10).toFixed(1) },
    { suffix: "cab", default01: 0.5, format: choice(["1x12", "2x12", "4x12"]) },
  ],

  delay: [
    { suffix: "time", default01: 0.4, format: hz(1, 4000, 350, "ms") },
    { suffix: "sync", default01: 0, format: choice(["Free", "Sync"]) },
    { suffix: "division", default01: 0.5, format: choice(["1/16", "1/8", "1/8.", "1/8T", "1/4", "1/4.", "1/4T", "1/2", "1/1"]) },
    { suffix: "feedback", default01: 0.32, format: pct },
    { suffix: "mix", default01: 0.3, format: pct },
    { suffix: "mode", default01: 0, format: choice(["Digital", "Tape"]) },
    { suffix: "pingpong", default01: 0, format: choice(["Off", "On"]) },
    { suffix: "tone", default01: 0.4, format: hz(500, 18000, 4000) },
    { suffix: "flutter", default01: 0.2, format: pct },
    { suffix: "offset", default01: 0.5, format: (v) => `${(v * 100 - 50).toFixed(1)} ms` },
  ],

  reverb: [
    { suffix: "size", default01: 0.5, format: pct },
    { suffix: "decay", default01: 0.6, format: pct },
    { suffix: "damping", default01: 0.5, format: pct },
    { suffix: "predelay", default01: 0.3, format: hz(0, 200, 30, "ms") },
    { suffix: "mix", default01: 0.3, format: pct },
    { suffix: "width", default01: 1, format: pct },
    { suffix: "freeze", default01: 0, format: choice(["Off", "Freeze"]) },
  ],

  lim: [
    { suffix: "ceiling", default01: 0.985, format: (v) => `${(v * 20 - 20).toFixed(1)} dBTP` },
    { suffix: "release", default01: 0.35, format: hz(1, 1000, 100, "ms") },
    { suffix: "lookahead", default01: 0.3, format: hz(0.5, 10, 2, "ms") },
  ],
};
