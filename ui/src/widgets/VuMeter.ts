/** Backlit VU meter with true needle ballistics.
 *
 *  The movement is modelled as the classic underdamped second-order system of
 *  a real VU (ANSI C16.5: 99% deflection in 300 ms, ~1% overshoot), i.e.
 *  natural frequency ~2.1 Hz, damping ratio ~0.85:
 *
 *      x'' = w^2 (target - x) - 2 z w x'
 *
 *  integrated per animation frame. Scale is -20..+3 VU over -46..+46 degrees.
 *  A peak LED latches for 500 ms above 0 VU.
 */

import { addTick } from "../animator";
import { dbFromLinear } from "../types";

const OMEGA = 2 * Math.PI * 2.1;
const ZETA = 0.85;
const ANGLE_MIN = -46;
const ANGLE_MAX = 46;
const DB_MIN = -20;
const DB_MAX = 3;

export class VuMeter {
  readonly el: HTMLElement;

  private needle: HTMLElement;
  private peakLed: HTMLElement;

  private position = 0;   // current needle deflection, 0..1
  private velocity = 0;
  private target = 0;
  private peakHold = 0;
  private removeTick: () => void;

  constructor(label = "VU", private mode: "vu" | "gr" = "vu") {
    this.el = document.createElement("div");
    this.el.className = "vu";
    this.el.setAttribute("role", "meter");
    this.el.setAttribute("aria-label", `${label} meter`);
    if (mode === "gr") this.position = 1; // GR needle rests at 0 (right)

    this.el.innerHTML = `
      <div class="vu-face">
        ${this.faceSvg()}
        <div class="vu-needle"></div>
        <div class="vu-pivot"></div>
        <div class="vu-glass"></div>
        <div class="vu-peak-led" title="peak"></div>
      </div>`;

    this.needle = this.el.querySelector<HTMLElement>(".vu-needle")!;
    this.peakLed = this.el.querySelector<HTMLElement>(".vu-peak-led")!;

    this.removeTick = addTick((dt) => this.tick(dt));
  }

  dispose(): void {
    this.removeTick();
  }

  /** Feed with a linear signal level (RMS-ish); 1.0 = 0 VU. */
  setLevel(linear: number): void {
    this.target = deflectionFromLinear(linear);
    if (dbFromLinear(linear) > 0) this.peakHold = 0.5;
  }

  /** GR mode: needle falls from 0 (right) as reduction increases. */
  setGrDb(db: number): void {
    const range = DB_MAX - DB_MIN; // 23 dB of scale travel
    this.target = Math.min(1.06, Math.max(0, 1 - Math.max(0, db) / range));
    if (db > 10) this.peakHold = 0.5;
  }

  private tick(dt: number): void {
    const accel =
      OMEGA * OMEGA * (this.target - this.position) - 2 * ZETA * OMEGA * this.velocity;
    this.velocity += accel * dt;
    this.position += this.velocity * dt;

    if (this.position < 0) {
      this.position = 0;
      this.velocity = Math.max(0, this.velocity); // mechanical end stop
    } else if (this.position > 1.06) {
      this.position = 1.06; // slam past +3 into the pin, like hardware
      this.velocity = Math.min(0, this.velocity);
    }

    const angle = ANGLE_MIN + this.position * (ANGLE_MAX - ANGLE_MIN);
    this.needle.style.transform = `rotate(${angle}deg)`;

    this.peakHold = Math.max(0, this.peakHold - dt);
    this.peakLed.classList.toggle("lit", this.peakHold > 0);
  }

  private faceSvg(): string {
    // The viewBox matches .vu-face 1:1 (148x72 px), so these are CSS pixels and
    // the SVG scale cannot drift away from the CSS needle. The pivot sits 4px
    // below the visible face, hidden behind the bezel like real hardware:
    // .vu-needle and .vu-pivot in styles.css must rotate about the same point.
    const cx = 74;
    const cy = 76;
    const rOuter = 67;

    const angleForDb = (db: number): number =>
      ANGLE_MIN + ((db - DB_MIN) / (DB_MAX - DB_MIN)) * (ANGLE_MAX - ANGLE_MIN);

    const point = (angleDeg: number, r: number): [number, number] => {
      const a = ((angleDeg - 90) * Math.PI) / 180;
      return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
    };

    // Red arc from 0 VU to +3.
    const [rx1, ry1] = point(angleForDb(0), rOuter - 6);
    const [rx2, ry2] = point(ANGLE_MAX, rOuter - 6);

    let ticks = "";
    const majors: [number, string][] =
      this.mode === "gr"
        ? // GR scale: 0 dB of reduction sits at the right-hand rest position.
          [[-20, "20"], [-15, "15"], [-10, "10"], [-6, "6"], [-3, "3"], [-1, "1"], [3, "0"]]
        : [[-20, "20"], [-10, "10"], [-7, "7"], [-5, "5"], [-3, "3"],
           [-2, "2"], [-1, "1"], [0, "0"], [1, "+1"], [2, "+2"], [3, "+3"]];

    for (const [db, legend] of majors) {
      const a = angleForDb(db);
      const [x1, y1] = point(a, rOuter - 9);
      const [x2, y2] = point(a, rOuter - 4.5);
      const [tx, ty] = point(a, rOuter - 13);
      const red = this.mode === "vu" && db >= 0 ? " red" : "";
      ticks += `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" class="vu-tick${red}"/>
                <text x="${tx}" y="${ty}" text-anchor="middle" class="vu-text${red}">${legend}</text>`;
    }

    for (let db = -20; db <= 3; db++) {
      const a = angleForDb(db);
      const [x1, y1] = point(a, rOuter - 7);
      const [x2, y2] = point(a, rOuter - 4.5);
      ticks += `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" class="vu-tick minor${db >= 0 ? " red" : ""}"/>`;
    }

    const redArc =
      this.mode === "vu"
        ? `<path d="M ${rx1} ${ry1} A ${rOuter - 6} ${rOuter - 6} 0 0 1 ${rx2} ${ry2}" class="vu-red-arc"/>`
        : "";
    const brand = this.mode === "gr" ? "GR" : "VU";

    return `<svg class="vu-scale" viewBox="0 0 148 72" preserveAspectRatio="xMidYMid meet" aria-hidden="true">
        ${redArc}
        ${ticks}
        <text x="74" y="60" text-anchor="middle" class="vu-brand">${brand}</text>
      </svg>`;
  }
}

/** Map linear level to 0..1 needle deflection across the -20..+3 dB scale. */
function deflectionFromLinear(linear: number): number {
  const db = dbFromLinear(linear);
  const clamped = Math.min(DB_MAX + 0.5, Math.max(DB_MIN - 6, db));
  return Math.min(1.06, Math.max(0, (clamped - DB_MIN) / (DB_MAX - DB_MIN)));
}
