/** Stereo LED ladder with correct dB spacing, per-segment colour zones,
 *  instant attack / exponential release and a 1.2 s hold-peak segment.
 *  Segment thresholds get denser towards 0 dBFS like real bargraph hardware.
 */

import { addTick } from "../animator";
import { dbFromLinear } from "../types";

const THRESHOLDS = [
  -60, -48, -42, -36, -30, -26, -22, -18, -15, -12, -9, -7, -5, -3, -1.5, 0,
];

const RELEASE_DB_PER_S = 26; // IEC-style fallback speed
const HOLD_SECONDS = 1.2;

export class LedLadder {
  readonly el: HTMLElement;

  private rows: HTMLElement[][] = [];
  private levels = [-120, -120];       // displayed level per channel, dB
  private peaks = [-120, -120];        // held peak per channel, dB
  private holds = [0, 0];
  private targets = [-120, -120];
  private removeTick: () => void;

  constructor(channels: 1 | 2 = 2) {
    this.el = document.createElement("div");
    this.el.className = "ladder";
    this.el.setAttribute("role", "meter");
    this.el.setAttribute("aria-label", "level meter");

    for (let ch = 0; ch < channels; ch++) {
      const row = document.createElement("div");
      row.className = "ladder-row";
      const segments: HTMLElement[] = [];

      for (const db of THRESHOLDS) {
        const seg = document.createElement("div");
        seg.className = `ladder-seg ${zoneFor(db)}`;
        row.appendChild(seg);
        segments.push(seg);
      }

      this.rows.push(segments);
      this.el.appendChild(row);
    }

    const legend = document.createElement("div");
    legend.className = "ladder-legend";
    for (const db of [-60, -30, -18, -9, -3, 0]) {
      const span = document.createElement("span");
      span.textContent = db === 0 ? "0" : String(db);
      span.style.left = `${positionFor(db) * 100}%`;
      legend.appendChild(span);
    }
    this.el.appendChild(legend);

    this.removeTick = addTick((dt) => this.tick(dt));
  }

  dispose(): void {
    this.removeTick();
  }

  /** Feed with linear peak levels; call as often as frames arrive. */
  setLevels(peakL: number, peakR?: number): void {
    this.targets[0] = dbFromLinear(peakL);
    if (this.rows.length > 1) this.targets[1] = dbFromLinear(peakR ?? peakL);
  }

  private tick(dt: number): void {
    for (let ch = 0; ch < this.rows.length; ch++) {
      const target = this.targets[ch];

      // Instant attack, timed release.
      this.levels[ch] =
        target >= this.levels[ch] ? target : Math.max(target, this.levels[ch] - RELEASE_DB_PER_S * dt);

      if (target >= this.peaks[ch] || this.holds[ch] <= 0) {
        this.peaks[ch] = target >= this.peaks[ch] ? target : this.levels[ch];
        this.holds[ch] = HOLD_SECONDS;
      } else {
        this.holds[ch] -= dt;
      }

      const segments = this.rows[ch];
      for (let i = 0; i < THRESHOLDS.length; i++) {
        const lit = this.levels[ch] >= THRESHOLDS[i];
        const held =
          this.peaks[ch] >= THRESHOLDS[i] &&
          (i === THRESHOLDS.length - 1 || this.peaks[ch] < THRESHOLDS[i + 1]);
        segments[i].classList.toggle("lit", lit);
        segments[i].classList.toggle("held", held && !lit);
      }
    }
  }
}

const zoneFor = (db: number): string => (db >= 0 ? "red" : db >= -9 ? "amber" : "green");

const positionFor = (db: number): number => {
  const i = THRESHOLDS.findIndex((t) => t >= db);
  return i < 0 ? 1 : i / (THRESHOLDS.length - 1);
};
