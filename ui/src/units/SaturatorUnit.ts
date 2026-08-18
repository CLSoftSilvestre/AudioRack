/** SAT-1 "Saturator" — 1U, warm bronze face.
 *  brand | TYPE selector | DRIVE BIAS OUTPUT | MIX + AUTO | VU + bypass
 */

import { VuMeter } from "../widgets/VuMeter";
import { paramID } from "../bridge/protocol";
import { paramKnob, paramSelector, paramSwitch, bypassControl, brandBlock, chassis } from "./unitKit";
import type { Store } from "../store";

export class SaturatorUnit {
  readonly el: HTMLElement;
  private vu: VuMeter;
  private unsubs: (() => void)[] = [];

  constructor(store: Store, slot: number) {
    const { root, face } = chassis("unit-sat");
    this.el = root;

    face.appendChild(brandBlock("SAT-1 &middot; SATURATOR", "TONE SERIES"));

    const id = (s: string) => paramID(slot, "sat", s);

    const typeWell = document.createElement("div");
    typeWell.className = "unit-section sat-type";
    const type = paramSelector(store, id("type"), "CHARACTER", 3);
    typeWell.appendChild(type.el);
    face.appendChild(typeWell);
    this.unsubs.push(type.unsub);

    const knobs = document.createElement("div");
    knobs.className = "unit-section sat-knobs";
    for (const [s, label, size, def] of [
      ["drive", "DRIVE", 56, 0.17],
      ["bias", "BIAS", 40, 0.5],
      ["out", "OUTPUT", 44, 0.67],
      ["mix", "MIX", 40, 1],
    ] as [string, string, number, number][]) {
      const k = paramKnob(store, id(s), { label, size, defaultValue01: def });
      knobs.appendChild(k.el);
      this.unsubs.push(k.unsub);
    }
    const auto = paramSwitch(store, id("autogain"), "AUTO");
    knobs.appendChild(auto.el);
    this.unsubs.push(auto.unsub);
    face.appendChild(knobs);

    const meterWell = document.createElement("div");
    meterWell.className = "unit-section sat-meter";
    this.vu = new VuMeter("output");
    meterWell.appendChild(this.vu.el);
    face.appendChild(meterWell);
    this.unsubs.push(store.onMeters(slot, (f) => this.vu.setLevel(Math.max(f.rmsL, f.rmsR) * 1.228)));

    const bypass = bypassControl(store, slot, this.el);
    face.appendChild(bypass.el);
    this.unsubs.push(bypass.unsub);
  }

  dispose(): void {
    this.unsubs.forEach((fn) => fn());
    this.vu.dispose();
  }
}
