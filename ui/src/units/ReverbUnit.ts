/** RV-8 "Reverb" — 2U, deep indigo face with a decay display.
 *  brand | SIZE DECAY DAMPING PREDELAY MIX WIDTH | DECAY display + FREEZE | bypass
 */

import { Display } from "../widgets/Display";
import { paramID } from "../bridge/protocol";
import { paramKnob, paramSwitch, bypassControl, brandBlock, chassis } from "./unitKit";
import type { Store } from "../store";

export class ReverbUnit {
  readonly el: HTMLElement;
  private display: Display;
  private unsubs: (() => void)[] = [];

  constructor(store: Store, slot: number) {
    const { root, face } = chassis("unit-reverb dark-face");
    this.el = root;

    face.appendChild(brandBlock("RV-8 &middot; REVERB", "FDN 8&times;8 &middot; TIME SERIES"));

    const id = (s: string) => paramID(slot, "reverb", s);

    // All six knobs in one row — 2U leaves room for a single tier.
    const knobs = document.createElement("div");
    knobs.className = "unit-section reverb-knobs";
    for (const [s, label, size, def] of [
      ["size", "SIZE", 48, 0.5],
      ["decay", "DECAY", 48, 0.6],
      ["damping", "DAMPING", 44, 0.5],
      ["predelay", "PREDELAY", 44, 0.3],
      ["mix", "MIX", 44, 0.3],
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

    const bypass = bypassControl(store, slot, this.el);
    face.appendChild(bypass.el);
    this.unsubs.push(bypass.unsub);
  }

  dispose(): void {
    this.unsubs.forEach((fn) => fn());
  }
}
