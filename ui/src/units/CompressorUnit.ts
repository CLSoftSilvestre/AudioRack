/** CMP-2 "Compressor" — 2U dynamics faceplate on dark charcoal.
 *
 *  brand | THRESHOLD RATIO KNEE / ATTACK RELEASE MAKEUP | mode switches |
 *  SC section | big GR VU meter | bypass
 */

import { VuMeter } from "../widgets/VuMeter";
import { paramID } from "../bridge/protocol";
import { paramKnob, paramSwitch, bypassControl, brandBlock, chassis } from "./unitKit";
import type { Store } from "../store";

export class CompressorUnit {
  readonly el: HTMLElement;

  private vu: VuMeter;
  private unsubs: (() => void)[] = [];

  constructor(store: Store, slot: number) {
    const { root, face } = chassis("unit-comp dark-face");
    this.el = root;

    face.appendChild(brandBlock("CMP-2 &middot; COMPRESSOR", "DYNAMICS SERIES"));

    const id = (suffix: string) => paramID(slot, "comp", suffix);

    // Two rows of three knobs.
    const knobGrid = document.createElement("div");
    knobGrid.className = "unit-section comp-knobs";

    const knobs: [string, string, number][] = [
      ["threshold", "THRESHOLD", 52],
      ["ratio", "RATIO", 52],
      ["knee", "KNEE", 44],
      ["attack", "ATTACK", 52],
      ["release", "RELEASE", 52],
      ["makeup", "MAKEUP", 44],
    ];

    for (const [suffix, label, size] of knobs) {
      const bound = paramKnob(store, id(suffix), { label, size, defaultValue01: 0.5 });
      knobGrid.appendChild(bound.el);
      this.unsubs.push(bound.unsub);
    }
    face.appendChild(knobGrid);

    // Mode switch bank.
    const switches = document.createElement("div");
    switches.className = "unit-section comp-switches";

    const toggles: [string, string][] = [
      ["topology", "FF / FB"],
      ["detector", "PK / RMS"],
      ["automakeup", "AUTO MK"],
      ["pdr", "PROG REL"],
    ];

    for (const [suffix, label] of toggles) {
      const bound = paramSwitch(store, id(suffix), label);
      switches.appendChild(bound.el);
      this.unsubs.push(bound.unsub);
    }
    face.appendChild(switches);

    // Sidechain section.
    const sc = document.createElement("div");
    sc.className = "unit-section comp-sc";
    sc.innerHTML = `<div class="section-title">SIDECHAIN</div>`;

    const hpf = paramKnob(store, id("schpf"), { label: "HPF", size: 40, defaultValue01: 0 });
    const ext = paramSwitch(store, id("scsource"), "INT / EXT");
    sc.appendChild(hpf.el);
    sc.appendChild(ext.el);
    this.unsubs.push(hpf.unsub, ext.unsub);
    face.appendChild(sc);

    // GR meter.
    const meterWell = document.createElement("div");
    meterWell.className = "unit-section comp-meter";
    this.vu = new VuMeter("gain reduction", "gr");
    meterWell.appendChild(this.vu.el);
    face.appendChild(meterWell);

    this.unsubs.push(store.onMeters(slot, (f) => this.vu.setGrDb(f.grDb)));

    const bypass = bypassControl(store, slot, this.el);
    face.appendChild(bypass.el);
    this.unsubs.push(bypass.unsub);
  }

  dispose(): void {
    this.unsubs.forEach((fn) => fn());
    this.vu.dispose();
  }
}
