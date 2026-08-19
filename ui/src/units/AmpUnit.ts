/** AMP-1 "Guitar Amp" — 3U, black-tolex face with a gold control strip and a
 *  grille cloth panel. Cascaded tube preamp, passive-style tone stack and a
 *  switchable speaker cabinet (1x12 / 2x12 / 4x12) drawn live from the CAB
 *  selector.
 *
 *  brand | CHANNEL | GAIN BASS MID TREBLE PRESENCE MASTER | CAB + speakers |
 *  grille | power
 */

import { paramID } from "../bridge/protocol";
import { paramKnob, paramSelector, bypassControl, brandBlock, chassis } from "./unitKit";
import type { Store } from "../store";

export class AmpUnit {
  readonly el: HTMLElement;
  private unsubs: (() => void)[] = [];

  constructor(store: Store, slot: number) {
    const { root, face } = chassis("unit-amp dark-face");
    this.el = root;

    face.appendChild(brandBlock("AMP-1 &middot; GUITAR AMP", "VALVE PREAMP SERIES"));

    const id = (s: string) => paramID(slot, "amp", s);

    // Channel voicing.
    const channelWell = document.createElement("div");
    channelWell.className = "unit-section amp-channel";
    const channel = paramSelector(store, id("channel"), "CHANNEL", 3);
    channelWell.appendChild(channel.el);
    face.appendChild(channelWell);
    this.unsubs.push(channel.unsub);

    // Preamp + tone + master, chicken-head knobs across a gold control strip.
    const knobs = document.createElement("div");
    knobs.className = "unit-section amp-knobs";
    for (const [s, label, def] of [
      ["gain", "GAIN", 0.5],
      ["bass", "BASS", 0.5],
      ["mid", "MIDDLE", 0.5],
      ["treble", "TREBLE", 0.5],
      ["presence", "PRESENCE", 0.5],
      ["master", "MASTER", 0.5],
    ] as [string, string, number][]) {
      const k = paramKnob(store, id(s), { label, size: 42, defaultValue01: def });
      knobs.appendChild(k.el);
      this.unsubs.push(k.unsub);
    }
    face.appendChild(knobs);

    // Cabinet selector with a live speaker diagram.
    const cabWell = document.createElement("div");
    cabWell.className = "unit-section amp-cab";
    const cab = paramSelector(store, id("cab"), "CABINET", 3);
    const speakers = document.createElement("div");
    speakers.className = "amp-speakers";
    speakers.innerHTML = `<i class="spk"></i><i class="spk"></i><i class="spk"></i><i class="spk"></i>`;
    cabWell.appendChild(speakers);
    cabWell.appendChild(cab.el);
    face.appendChild(cabWell);
    this.unsubs.push(cab.unsub);
    this.unsubs.push(
      store.onParam(id("cab"), (p) => {
        const idx = Math.round(p.value01 * 2); // 0..2
        speakers.dataset.cab = ["1x12", "2x12", "4x12"][idx] ?? "2x12";
      }),
    );

    // Grille cloth panel filling the rest of the face.
    const grille = document.createElement("div");
    grille.className = "unit-section amp-grille";
    face.appendChild(grille);

    const bypass = bypassControl(store, slot, this.el);
    face.appendChild(bypass.el);
    this.unsubs.push(bypass.unsub);
  }

  dispose(): void {
    this.unsubs.forEach((fn) => fn());
  }
}
