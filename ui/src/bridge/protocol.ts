/** Typed mirror of the native bridge protocol (see src/ui/RackWebView.h).
 *  Wire format is JSON over JUCE's native-integration event channel.
 */

export const MAX_SLOTS = 12;

/** Analyser band layout — mirror of src/core/CoreTypes.h. Bands are spaced
 *  geometrically from SPECTRUM_MIN_HZ to SPECTRUM_MAX_HZ. */
export const SPECTRUM_BANDS = 96;
export const SPECTRUM_MIN_HZ = 20;
export const SPECTRUM_MAX_HZ = 22000;
export const SPECTRUM_FLOOR_DB = -100;

/** Centre frequency of analyser band `b` (band edges are b and b+1). */
export const spectrumBandHz = (b: number): number =>
  SPECTRUM_MIN_HZ * Math.pow(SPECTRUM_MAX_HZ / SPECTRUM_MIN_HZ, (b + 0.5) / SPECTRUM_BANDS);

/** Module type metadata, as registered in the native ModuleRegistry. */
export interface ModuleInfo {
  id: string;
  name: string;
  category: string;
  units: number; // rack units (1U/2U/3U)
}

/** native -> web: "ar_rack" */
export interface RackMessage {
  slots: string[]; // module id per slot, "" = empty
  modules: ModuleInfo[];
  version?: string; // plugin version (JucePlugin_VersionString)
}

/** native -> web: "ar_params" — batch of [paramID, value01, displayText] */
export interface ParamsMessage {
  p: [string, number, string][];
}

/** native -> web: "ar_meters" — batch of [slot, peakL, peakR, rmsL, rmsR, grDb] */
export interface MetersMessage {
  m: [number, number, number, number, number, number][];
}

/** native -> web: "ar_spectrum" — one entry per module that publishes an RTA:
 *  [slot, ...bands], where each band is dBFS x 2 rounded to an integer (half-dB
 *  steps, finer than a display pixel, ~a third of the JSON of raw floats). */
export interface SpectrumMessage {
  s: number[][];
}

/** native -> web: "ar_ab" — which parameter bank (A=0 / B=1) is live. */
export interface AbMessage {
  bank: number;
}

/** native -> web: "ar_midi" — MIDI-learn state.
 *  `armed` is the paramID waiting for a CC (null = nothing armed);
 *  `map` lists every current [paramID, ccNumber] binding. */
export interface MidiMessage {
  armed: string | null;
  map: [string, number][];
}

/** web -> native: everything travels on the "ar_ui" event. */
export type UiEvent =
  | { type: "ready" }
  | { type: "setParam"; id: string; value01: number }
  | { type: "beginGesture"; id: string }
  | { type: "endGesture"; id: string }
  | { type: "mount"; slot: number; moduleId: string }
  | { type: "unmount"; slot: number }
  | { type: "move"; from: number; to: number }
  | { type: "duplicate"; from: number; to: number }
  | { type: "abSelect"; bank: number }
  | { type: "abCopy" }
  | { type: "midiLearn"; id: string }
  | { type: "midiClearLearn" }
  | { type: "midiForget"; id: string }
  | { type: "openUrl"; url: string };

/** Parameter ID helpers — must match src/core/ParameterModel.h. */
export const paramID = (slot: number, moduleId: string, suffix: string): string =>
  `slot${slot}.${moduleId}.${suffix}`;

export const slotParamID = (slot: number, suffix: "bypass" | "mix"): string =>
  `slot${slot}.${suffix}`;
