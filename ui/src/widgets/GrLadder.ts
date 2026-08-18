/** Horizontal gain-reduction LED strip: 0..20 dB right-to-left in amber/red,
 *  instant attack, timed release, like the GR displays on digital dynamics
 *  hardware.
 */

import { addTick } from "../animator";

const STEPS = [1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 18, 20];
const RELEASE_DB_PER_S = 18;

export class GrLadder {
  readonly el: HTMLElement;

  private segments: HTMLElement[] = [];
  private displayed = 0;
  private target = 0;
  private removeTick: () => void;

  constructor(label = "GR") {
    this.el = document.createElement("div");
    this.el.className = "gr-ladder";
    this.el.setAttribute("role", "meter");
    this.el.setAttribute("aria-label", "gain reduction");

    const row = document.createElement("div");
    row.className = "gr-row";
    for (const db of STEPS) {
      const seg = document.createElement("div");
      seg.className = `gr-seg ${db >= 10 ? "deep" : ""}`;
      row.appendChild(seg);
      this.segments.push(seg);
    }
    this.el.appendChild(row);

    const legend = document.createElement("div");
    legend.className = "gr-legend";
    legend.innerHTML = `<span>${label}</span><span>1</span><span>5</span><span>10</span><span>20</span>`;
    this.el.appendChild(legend);

    this.removeTick = addTick((dt) => this.tick(dt));
  }

  dispose(): void {
    this.removeTick();
  }

  setGrDb(db: number): void {
    this.target = Math.max(0, db);
  }

  private tick(dt: number): void {
    this.displayed =
      this.target >= this.displayed
        ? this.target
        : Math.max(this.target, this.displayed - RELEASE_DB_PER_S * dt);

    for (let i = 0; i < STEPS.length; i++) {
      this.segments[i].classList.toggle("lit", this.displayed >= STEPS[i]);
    }
  }
}
