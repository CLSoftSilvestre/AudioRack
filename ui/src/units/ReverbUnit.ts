/** RV-8 "Reverb" — 3U, deep indigo face with a large decay display.
 *  brand | SIZE DECAY DAMPING | PREDELAY MIX WIDTH | FREEZE | VU + bypass
 */

import { VuMeter } from "../widgets/VuMeter";
import { Display } from "../widgets/Display";
import { paramID } from "../bridge/protocol";
import { paramKnob, paramSwitch, bypassControl, brandBlock, chassis } from "./unitKit";
import type { Store } from "../store";

export class ReverbUnit {
  readonly el: HTMLElement;
  private vu: VuMeter;
  private display: Display;
  private unsubs: (() => void)[] = [];

  constructor(store: Store, slot: number) {
    const { root, face } = chassis("unit-reverb dark-face");
    this.el = root;

    face.appendChild(brandBlock("RV-8 &middot; REVERB", "FDN 8&times;8 &middot; TIME SERIES"));

    const id = (s: string) => paramID(slot, "reverb", s);

    // Big knobs.
    const knobs = document.createElement("div");
    knobs.className = "unit-section reverb-knobs";
    for (const [s, label, size, def] of [
      ["size", "SIZE", 56, 0.5],
      ["decay", "DECAY", 56, 0.6],
      ["damping", "DAMPING", 52, 0.5],
      ["predelay", "PREDELAY", 44, 0.3],
      ["mix", "MIX", 48, 0.3],
      ["width", "WIDTH", 44, 1],
    ] as [string, string, number, number][]) {
      const k = paramKnob(store, id(s), { label, size, defaultValue01: def });
      knobs.appendChild(k.el);
      this.unsubs.push(k.unsub);
    }
    face.appendChild(knobs);

    // Freeze + decay display.
    const center = document.createElement("div");
    center.className = "unit-section reverb-center";
    this.display = new Display("DECAY", "green");
    const freeze = paramSwitch(store, id("freeze"), "FREEZE");
    center.appendChild(this.display.el);
    center.appendChild(freeze.el);
    face.appendChild(center);
    this.unsubs.push(freeze.unsub);
    this.unsubs.push(
      store.onParam(id("decay"), (p) => {
        // Rough RT from the DSP mapping (0.5..12 s).
        const rt = 0.5 + p.value01 * 11.5;
        this.display.setText(`${rt.toFixed(1)} s`);
      }),
    );

    const meterWell = document.createElement("div");
    meterWell.className = "unit-section reverb-meter";
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
