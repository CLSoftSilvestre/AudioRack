/** GN-1 "Gain" — 1U utility faceplate.
 *
 *  Layout, left to right on brushed champagne aluminium:
 *    rack ear (screws) | brand block | GAIN knob with dB scale |
 *    stereo LED ladder | VU meter | bypass bat switch + power LED | rack ear
 */

import { Knob } from "../widgets/Knob";
import { LedLadder } from "../widgets/LedLadder";
import { VuMeter } from "../widgets/VuMeter";
import { Switch } from "../widgets/Switch";
import { screw } from "../widgets/Screw";
import { paramSelector } from "./unitKit";
import { paramID, slotParamID } from "../bridge/protocol";
import type { Store } from "../store";

export class GainUnit {
  readonly el: HTMLElement;

  private ladder: LedLadder;
  private vu: VuMeter;
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
        <div class="unit-section unit-vu-well"></div>
        <div class="unit-section unit-bypass-well">
          <div class="power-led"></div>
        </div>
      </div>
      <div class="unit-ear right"></div>`;

    const gainId = paramID(slot, "gain", "gaindb");
    const bypassId = slotParamID(slot, "bypass");

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

    // VU (RMS) meter.
    this.vu = new VuMeter("output");
    this.el.querySelector(".unit-vu-well")!.appendChild(this.vu.el);

    this.unsubscribers.push(
      store.onMeters(slot, (f) => {
        this.ladder.setLevels(f.peakL, f.peakR);
        this.vu.setLevel(Math.max(f.rmsL, f.rmsR) * 1.228); // 0 VU ref = -1.78 dBFS
      }),
    );

    // Bypass switch + power LED (lit = processing).
    const bypassSwitch = new Switch({
      label: "IN",
      onChange: (on) => store.setParam(bypassId, on ? 0 : 1),
    });
    const led = this.el.querySelector<HTMLElement>(".power-led")!;
    this.el.querySelector(".unit-bypass-well")!.prepend(bypassSwitch.el);
    this.unsubscribers.push(
      store.onParam(bypassId, (p) => {
        const bypassed = p.value01 >= 0.5;
        bypassSwitch.setOn(!bypassed);
        led.classList.toggle("lit", !bypassed);
        this.el.classList.toggle("bypassed", bypassed);
      }),
    );
  }

  dispose(): void {
    this.unsubscribers.forEach((fn) => fn());
    this.ladder.dispose();
    this.vu.dispose();
  }
}
