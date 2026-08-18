/** LM-1 "Peak Limiter" — 1U, near-black face with red accent.
 *  brand | CEILING RELEASE LOOKAHEAD | GR ladder + TP LED | bypass
 */

import { GrLadder } from "../widgets/GrLadder";
import { paramID } from "../bridge/protocol";
import { paramKnob, bypassControl, brandBlock, chassis } from "./unitKit";
import type { Store } from "../store";

export class LimiterUnit {
  readonly el: HTMLElement;

  private ladder: GrLadder;
  private unsubs: (() => void)[] = [];

  constructor(store: Store, slot: number) {
    const { root, face } = chassis("unit-lim dark-face");
    this.el = root;

    face.appendChild(brandBlock("LM-1 &middot; PEAK LIMITER", "TRUE PEAK &middot; 4&times; OS"));

    const id = (suffix: string) => paramID(slot, "lim", suffix);

    const knobRow = document.createElement("div");
    knobRow.className = "unit-section lim-knobs";

    for (const [suffix, label, size] of [
      ["ceiling", "CEILING", 56],
      ["release", "RELEASE", 48],
      ["lookahead", "LOOKAHEAD", 48],
    ] as [string, string, number][]) {
      const bound = paramKnob(store, id(suffix), { label, size, defaultValue01: 0.5 });
      knobRow.appendChild(bound.el);
      this.unsubs.push(bound.unsub);
    }
    face.appendChild(knobRow);

    const meterWell = document.createElement("div");
    meterWell.className = "unit-section lim-meter";
    this.ladder = new GrLadder("GR");

    const tpLed = document.createElement("div");
    tpLed.className = "tp-led";
    tpLed.innerHTML = `<div class="power-led red"></div><span>TP</span>`;

    meterWell.appendChild(this.ladder.el);
    meterWell.appendChild(tpLed);
    face.appendChild(meterWell);

    const led = tpLed.querySelector<HTMLElement>(".power-led")!;
    let tpHold = 0;
    this.unsubs.push(
      store.onMeters(slot, (f) => {
        this.ladder.setGrDb(f.grDb);
        if (f.grDb > 0.3) tpHold = Date.now() + 400;
        led.classList.toggle("lit", Date.now() < tpHold);
      }),
    );

    const bypass = bypassControl(store, slot, this.el);
    face.appendChild(bypass.el);
    this.unsubs.push(bypass.unsub);
  }

  dispose(): void {
    this.unsubs.forEach((fn) => fn());
    this.ladder.dispose();
  }
}
