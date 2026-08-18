/** Shared UI-side data shapes. */

export interface MeterFrameData {
  peakL: number;
  peakR: number;
  rmsL: number;
  rmsR: number;
  grDb: number;
}

export const dbFromLinear = (v: number): number =>
  v <= 1e-6 ? -120 : 20 * Math.log10(v);
