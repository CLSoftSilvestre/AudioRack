/** Typed mirror of the native bridge protocol (see src/ui/RackWebView.h).
 *  Wire format is JSON over JUCE's native-integration event channel.
 */

export const MAX_SLOTS = 12;

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
}

/** native -> web: "ar_params" — batch of [paramID, value01, displayText] */
export interface ParamsMessage {
  p: [string, number, string][];
}

/** native -> web: "ar_meters" — batch of [slot, peakL, peakR, rmsL, rmsR, grDb] */
export interface MetersMessage {
  m: [number, number, number, number, number, number][];
}

/** web -> native: everything travels on the "ar_ui" event. */
export type UiEvent =
  | { type: "ready" }
  | { type: "setParam"; id: string; value01: number }
  | { type: "beginGesture"; id: string }
  | { type: "endGesture"; id: string }
  | { type: "mount"; slot: number; moduleId: string }
  | { type: "unmount"; slot: number }
  | { type: "move"; from: number; to: number };

/** Parameter ID helpers — must match src/core/ParameterModel.h. */
export const paramID = (slot: number, moduleId: string, suffix: string): string =>
  `slot${slot}.${moduleId}.${suffix}`;

export const slotParamID = (slot: number, suffix: "bypass" | "mix"): string =>
  `slot${slot}.${suffix}`;
