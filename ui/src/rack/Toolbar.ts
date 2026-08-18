/** Top rack strip: brand wordmark on the left, A/B compare on the right.
 *
 *  A and B are two full parameter snapshots the host keeps; the rack layout is
 *  shared. Selecting a bank recalls its values (knobs morph to them); COPY makes
 *  the inactive bank equal to the live one, so you can A/B two variations of the
 *  same starting point.
 */

import type { Store } from "../store";

export class Toolbar {
  readonly el: HTMLElement;
  private buttonA: HTMLButtonElement;
  private buttonB: HTMLButtonElement;

  constructor(private store: Store) {
    this.el = document.createElement("div");
    this.el.className = "rack-toolbar";
    this.el.innerHTML = `
      <div class="toolbar-brand">
        <span class="toolbar-logo">AUDIO<span>RACK</span></span>
        <span class="toolbar-tag">EFFECTS RACK</span>
      </div>
      <div class="toolbar-ab">
        <span class="ab-label">COMPARE</span>
        <button class="ab-btn" data-bank="0">A</button>
        <button class="ab-btn" data-bank="1">B</button>
        <button class="ab-copy" title="Copy the live bank onto the other">COPY →</button>
      </div>`;

    this.buttonA = this.el.querySelector<HTMLButtonElement>('.ab-btn[data-bank="0"]')!;
    this.buttonB = this.el.querySelector<HTMLButtonElement>('.ab-btn[data-bank="1"]')!;

    this.buttonA.addEventListener("click", () => this.store.selectBank(0));
    this.buttonB.addEventListener("click", () => this.store.selectBank(1));
    this.el
      .querySelector<HTMLButtonElement>(".ab-copy")!
      .addEventListener("click", () => this.store.copyBank());

    this.store.onAb((bank) => {
      this.buttonA.classList.toggle("active", bank === 0);
      this.buttonB.classList.toggle("active", bank === 1);
    });
  }
}
