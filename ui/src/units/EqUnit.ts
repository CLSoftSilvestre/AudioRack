/** EQ-6 "Parametric EQ" — 3U, graphite face. A tall live response curve on the
 *  left; the six band strips (type selector + freq/gain/Q knobs + on) sit to its
 *  right at full height so every control fits; output trim + bypass on the far
 *  right.
 */

import { EqCurve, type EqBandValues } from "../widgets/EqCurve";
import { paramID } from "../bridge/protocol";
import { paramKnob, paramSwitch, paramSelector, bypassControl, brandBlock, chassis } from "./unitKit";
import type { Store } from "../store";

const BANDS = 6;

export class EqUnit {
  readonly el: HTMLElement;
  private curve: EqCurve;
  private model: EqBandValues[] = [];
  private unsubs: (() => void)[] = [];

  constructor(private store: Store, slot: number) {
    const { root, face } = chassis("unit-eq dark-face");
    this.el = root;
    face.classList.add("eq-face");

    const id = (i: number, f: string) => paramID(slot, "eq", `b${i}${f}`);

    for (let i = 0; i < BANDS; i++) {
      this.model.push({ type: 0, freq: 1000, gain: 0, q: 0.71, on: true, solo: false });
    }

    // Left column: brand + tall response curve.
    const top = document.createElement("div");
    top.className = "eq-left";
    top.appendChild(brandBlock("EQ-6 &middot; PARAMETRIC", "EQ SERIES"));

    this.curve = new EqCurve(
      430,
      196,
      (band, freq, gain) => {
        this.store.setParam(id(band, "freq"), this.freqTo01(freq));
        if (this.model[band].type <= 2)
          this.store.setParam(id(band, "gain"), (gain + 18) / 36);
      },
      (band, active) => {
        const p = id(band, "freq");
        active ? this.store.beginGesture(p) : this.store.endGesture(p);
      },
    );
    top.appendChild(this.curve.el);
    face.appendChild(top);

    // Band strips.
    const strips = document.createElement("div");
    strips.className = "eq-strips";

    for (let i = 0; i < BANDS; i++) {
      const strip = document.createElement("div");
      strip.className = "eq-strip";
      strip.style.setProperty("--band-color", BAND_COLORS[i]);

      const type = paramSelector(this.store, id(i, "type"), `B${i + 1}`, 5);
      const freq = paramKnob(this.store, id(i, "freq"), { label: "FREQ", size: 34, defaultValue01: 0.5 });
      const gain = paramKnob(this.store, id(i, "gain"), { label: "GAIN", size: 34, defaultValue01: 0.5 });
      const q = paramKnob(this.store, id(i, "q"), { label: "Q", size: 30, defaultValue01: 0.28 });
      const on = paramSwitch(this.store, id(i, "on"), "ON");

      strip.appendChild(type.el);
      strip.appendChild(freq.el);
      strip.appendChild(gain.el);
      strip.appendChild(q.el);
      strip.appendChild(on.el);
      strips.appendChild(strip);

      this.unsubs.push(type.unsub, freq.unsub, gain.unsub, q.unsub, on.unsub);

      // Track model values for the curve by parsing host display text.
      this.unsubs.push(
        this.store.onParam(id(i, "type"), (p) => { this.model[i].type = Math.round(p.value01 * 4); this.curve.setBands(this.model); }),
        this.store.onParam(id(i, "freq"), (p) => { this.model[i].freq = parseHz(p.text); this.curve.setBands(this.model); }),
        this.store.onParam(id(i, "gain"), (p) => { this.model[i].gain = parseNum(p.text); this.curve.setBands(this.model); }),
        this.store.onParam(id(i, "q"), (p) => { this.model[i].q = parseNum(p.text) || 0.71; this.curve.setBands(this.model); }),
        this.store.onParam(id(i, "on"), (p) => { this.model[i].on = p.value01 >= 0.5; this.curve.setBands(this.model); }),
      );
    }
    face.appendChild(strips);

    // Right rail: trim + bypass.
    const rail = document.createElement("div");
    rail.className = "eq-rail";
    const trim = paramKnob(this.store, paramID(slot, "eq", "trim"), { label: "TRIM", size: 40, defaultValue01: 0.5 });
    rail.appendChild(trim.el);
    this.unsubs.push(trim.unsub);
    face.appendChild(rail);

    const bypass = bypassControl(this.store, slot, this.el);
    face.appendChild(bypass.el);
    this.unsubs.push(bypass.unsub);

    this.curve.setBands(this.model);
  }

  private freqTo01(hz: number): number {
    const min = 20, max = 20000, centre = 632;
    const s = Math.log(0.5) / Math.log((centre - min) / (max - min));
    return Math.pow((hz - min) / (max - min), s);
  }

  dispose(): void {
    this.unsubs.forEach((fn) => fn());
  }
}

const BAND_COLORS = ["#ff6b6b", "#ffa94d", "#ffd43b", "#69db7c", "#4dabf7", "#b197fc"];

function parseNum(text: string): number {
  const m = text.match(/-?\d+(\.\d+)?/);
  return m ? parseFloat(m[0]) : 0;
}
function parseHz(text: string): number {
  const n = parseNum(text);
  return /k/i.test(text) ? n * 1000 : n;
}
