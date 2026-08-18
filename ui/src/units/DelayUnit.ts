/** DL-2 "Delay" — 2U, deep teal face.
 *  brand | TIME + sync/division | FEEDBACK TONE MIX | mode/pingpong |
 *  flutter/offset | LED meter + bypass
 */

import { LedLadder } from "../widgets/LedLadder";
import { paramID } from "../bridge/protocol";
import { paramKnob, paramSelector, paramSwitch, bypassControl, brandBlock, chassis } from "./unitKit";
import type { Store } from "../store";

export class DelayUnit {
  readonly el: HTMLElement;
  private ladder: LedLadder;
  private unsubs: (() => void)[] = [];

  constructor(store: Store, slot: number) {
    const { root, face } = chassis("unit-delay dark-face");
    this.el = root;

    face.appendChild(brandBlock("DL-2 &middot; DELAY", "TIME SERIES"));

    const id = (s: string) => paramID(slot, "delay", s);

    // Time section: big time knob + sync/division selectors.
    const timeWell = document.createElement("div");
    timeWell.className = "unit-section delay-time";
    const time = paramKnob(store, id("time"), { label: "TIME", size: 56, defaultValue01: 0.4 });
    const sync = paramSelector(store, id("sync"), "MODE", 2);
    const div = paramSelector(store, id("division"), "DIV", 9);
    timeWell.appendChild(time.el);
    const stack = document.createElement("div");
    stack.className = "selector-stack";
    stack.appendChild(sync.el);
    stack.appendChild(div.el);
    timeWell.appendChild(stack);
    face.appendChild(timeWell);
    this.unsubs.push(time.unsub, sync.unsub, div.unsub);

    // Feedback / tone / mix.
    const knobs = document.createElement("div");
    knobs.className = "unit-section delay-knobs";
    for (const [s, label, def] of [
      ["feedback", "FEEDBACK", 0.32],
      ["tone", "TONE", 0.4],
      ["mix", "MIX", 0.3],
      ["flutter", "FLUTTER", 0.2],
      ["offset", "STEREO", 0.5],
    ] as [string, string, number][]) {
      const k = paramKnob(store, id(s), { label, size: 40, defaultValue01: def });
      knobs.appendChild(k.el);
      this.unsubs.push(k.unsub);
    }
    face.appendChild(knobs);

    // Character switches.
    const sw = document.createElement("div");
    sw.className = "unit-section delay-switches";
    const mode = paramSwitch(store, id("mode"), "TAPE");
    const ping = paramSwitch(store, id("pingpong"), "PING");
    sw.appendChild(mode.el);
    sw.appendChild(ping.el);
    face.appendChild(sw);
    this.unsubs.push(mode.unsub, ping.unsub);

    const meterWell = document.createElement("div");
    meterWell.className = "unit-section delay-meter";
    this.ladder = new LedLadder(2);
    meterWell.appendChild(this.ladder.el);
    face.appendChild(meterWell);
    this.unsubs.push(store.onMeters(slot, (f) => this.ladder.setLevels(f.peakL, f.peakR)));

    const bypass = bypassControl(store, slot, this.el);
    face.appendChild(bypass.el);
    this.unsubs.push(bypass.unsub);
  }

  dispose(): void {
    this.unsubs.forEach((fn) => fn());
    this.ladder.dispose();
  }
}
