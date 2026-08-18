/** GT-1 "Gate" — 1U on steel blue-grey.
 *  brand | THRESH HYST RANGE ATTACK HOLD RELEASE | SC HPF LOOKAHEAD |
 *  open/closed status LED | bypass
 */

import { paramID } from "../bridge/protocol";
import { paramKnob, paramSwitch, bypassControl, brandBlock, chassis } from "./unitKit";
import type { Store } from "../store";

export class GateUnit {
  readonly el: HTMLElement;

  private unsubs: (() => void)[] = [];

  constructor(store: Store, slot: number) {
    const { root, face } = chassis("unit-gate dark-face");
    this.el = root;

    face.appendChild(brandBlock("GT-1 &middot; GATE", "DYNAMICS SERIES"));

    const id = (suffix: string) => paramID(slot, "gate", suffix);

    const knobRow = document.createElement("div");
    knobRow.className = "unit-section gate-knobs";

    for (const [suffix, label] of [
      ["threshold", "THRESH"],
      ["hysteresis", "HYST"],
      ["range", "RANGE"],
      ["attack", "ATTACK"],
      ["hold", "HOLD"],
      ["release", "RELEASE"],
      ["schpf", "SC HPF"],
      ["lookahead", "LOOKAHD"],
    ] as [string, string][]) {
      const bound = paramKnob(store, id(suffix), { label, size: 40, defaultValue01: 0.5 });
      knobRow.appendChild(bound.el);
      this.unsubs.push(bound.unsub);
    }
    face.appendChild(knobRow);

    // Sidechain source + gate status.
    const status = document.createElement("div");
    status.className = "unit-section gate-status";

    const ext = paramSwitch(store, id("scsource"), "INT / EXT");
    status.appendChild(ext.el);
    this.unsubs.push(ext.unsub);

    const openLed = document.createElement("div");
    openLed.className = "gate-open-led";
    openLed.innerHTML = `<div class="power-led"></div><span>OPEN</span>`;
    status.appendChild(openLed);
    face.appendChild(status);

    const led = openLed.querySelector<HTMLElement>(".power-led")!;
    this.unsubs.push(
      store.onMeters(slot, (f) => led.classList.toggle("lit", f.grDb < 6)),
    );

    const bypass = bypassControl(store, slot, this.el);
    face.appendChild(bypass.el);
    this.unsubs.push(bypass.unsub);
  }

  dispose(): void {
    this.unsubs.forEach((fn) => fn());
  }
}
