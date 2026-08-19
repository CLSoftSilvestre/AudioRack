/** GN-1 "Gain" — 1U utility faceplate.
 *
 *  Layout, left to right on brushed champagne aluminium:
 *    rack ear (screws) | brand block | GAIN knob with dB scale |
 *    stereo LED ladder | bypass bat switch + power LED | rack ear
 */

import { Knob } from "../widgets/Knob";
import { LedLadder } from "../widgets/LedLadder";
import { screw } from "../widgets/Screw";
import { paramSelector, bypassControl } from "./unitKit";
import { paramID } from "../bridge/protocol";
import type { Store } from "../store";

export class GainUnit {
  readonly el: HTMLElement;

  private ladder: LedLadder;
  private unsubscribers: (() => void)[] = [];

  constructor(store: Store, slot: number) {
    this.el = document.createElement("div");
    this.el.className = "unit unit-gain";
    this.el.innerHTML = `
      <div class="unit-ear left"></div>
      <div class="unit-face">
        <div class="unit-brand">
          <div class="unit-logo">AUDIO<span>RACK</span></div>
          <div class="unit-model">GN-1 &middot; GAIN</div>
          <div class="unit-series">UTILITY SERIES</div>
        </div>
        <div class="unit-section unit-knob-well"></div>
        <div class="unit-section unit-chmode-well"></div>
        <div class="unit-section unit-meter-well">
          <div class="silkscreen">LEVEL</div>
        </div>
      </div>
      <div class="unit-ear right"></div>`;

    const gainId = paramID(slot, "gain", "gaindb");

    // Rack-ear screws.
    for (const side of ["left", "right"] as const) {
      const ear = this.el.querySelector<HTMLElement>(`.unit-ear.${side}`)!;
      ear.appendChild(screw(13));
      ear.appendChild(screw(13));
    }

    // GAIN knob.
    const knob = new Knob({
      label: "GAIN",
      size: 62,
      defaultValue01: 0.5,
      ticks: ["-60", "-30", "0", "+12"],
      onInput: (v) => store.setParam(gainId, v),
      onGestureStart: () => store.beginGesture(gainId),
      onGestureEnd: () => store.endGesture(gainId),
    });
    this.el.querySelector(".unit-knob-well")!.appendChild(knob.el);
    this.unsubscribers.push(store.onParam(gainId, (p) => knob.setValue(p.value01, p.text)));

    // Channel mode: Stereo / Mono / Left / Right. Left copies input 1 to both
    // channels — the fix for a mono source (e.g. a guitar) on one input.
    const chmode = paramSelector(store, paramID(slot, "gain", "chmode"), "CHANNELS", 4);
    this.el.querySelector(".unit-chmode-well")!.appendChild(chmode.el);
    this.unsubscribers.push(chmode.unsub);

    // Stereo LED ladder fed from peak meters.
    this.ladder = new LedLadder(2);
    this.el.querySelector(".unit-meter-well")!.appendChild(this.ladder.el);

    this.unsubscribers.push(
      store.onMeters(slot, (f) => this.ladder.setLevels(f.peakL, f.peakR)),
    );

    // Unit on/off: illuminated rocker (lit red = processing).
    const bypass = bypassControl(store, slot, this.el);
    this.el.querySelector(".unit-face")!.appendChild(bypass.el);
    this.unsubscribers.push(bypass.unsub);
  }

  dispose(): void {
    this.unsubscribers.forEach((fn) => fn());
    this.ladder.dispose();
  }
}
