/** The 19" rack: side posts with square mounting holes, numbered slots,
 *  mounted units. Rebuilds units only when the layout actually changes;
 *  meter/param updates never touch this layer.
 *
 *  Interactions here are the M3 minimum: double-click an empty slot to mount
 *  a module, right-click a unit for bypass/remove. Drag-reorder, A/B and the
 *  browser panel arrive with M7.
 */

import { GainUnit } from "../units/GainUnit";
import { MAX_SLOTS, slotParamID } from "../bridge/protocol";
import type { Store } from "../store";

interface Mounted {
  unit: GainUnit;
  moduleId: string;
}

export class RackFrame {
  readonly el: HTMLElement;

  private slotEls: HTMLElement[] = [];
  private mounted = new Map<number, Mounted>();
  private menu: HTMLElement | null = null;

  constructor(private store: Store) {
    this.el = document.createElement("div");
    this.el.className = "rack";
    this.el.innerHTML = `
      <div class="rack-post left"></div>
      <div class="rack-bay"></div>
      <div class="rack-post right"></div>`;

    const bay = this.el.querySelector<HTMLElement>(".rack-bay")!;

    for (let slot = 0; slot < MAX_SLOTS; slot++) {
      const slotEl = document.createElement("div");
      slotEl.className = "rack-slot empty";
      slotEl.dataset.slot = String(slot);
      slotEl.innerHTML = `
        <div class="blank-panel">
          <div class="blank-vents">${"<i></i>".repeat(10)}</div>
          <div class="blank-hint">double-click to mount a module</div>
        </div>`;

      slotEl.addEventListener("dblclick", () => {
        if (!this.mounted.has(slot)) {
          const first = this.store.availableModules()[0];
          if (first) this.store.mount(slot, first.id);
        }
      });

      slotEl.addEventListener("contextmenu", (e) => {
        e.preventDefault();
        if (this.mounted.has(slot)) this.openMenu(e, slot);
      });

      bay.appendChild(slotEl);
      this.slotEls.push(slotEl);
    }

    document.addEventListener("pointerdown", (e) => {
      if (this.menu && !this.menu.contains(e.target as Node)) this.closeMenu();
    });

    store.onRack((slots) => this.sync(slots));
  }

  private sync(slots: string[]): void {
    for (let slot = 0; slot < MAX_SLOTS; slot++) {
      const wanted = slots[slot] ?? "";
      const current = this.mounted.get(slot);

      if (current && current.moduleId === wanted) continue;

      if (current) {
        current.unit.dispose();
        current.unit.el.remove();
        this.mounted.delete(slot);
        this.slotEls[slot].classList.add("empty");
      }

      if (wanted !== "") {
        // Registry currently ships Gain only; a unit factory keyed on module
        // id slots in here as M5/M6 add faceplates.
        const unit = new GainUnit(this.store, slot);
        this.slotEls[slot].appendChild(unit.el);
        this.slotEls[slot].classList.remove("empty");
        this.mounted.set(slot, { unit, moduleId: wanted });
      }
    }
  }

  private openMenu(e: MouseEvent, slot: number): void {
    this.closeMenu();

    const bypassId = slotParamID(slot, "bypass");
    const bypassed = this.store.param(bypassId).value01 >= 0.5;

    const menu = document.createElement("div");
    menu.className = "context-menu";

    const addItem = (label: string, action: () => void) => {
      const item = document.createElement("button");
      item.className = "context-item";
      item.textContent = label;
      item.addEventListener("click", () => {
        action();
        this.closeMenu();
      });
      menu.appendChild(item);
    };

    addItem(bypassed ? "Enable" : "Bypass", () =>
      this.store.setParam(bypassId, bypassed ? 0 : 1),
    );
    addItem("Remove", () => this.store.unmount(slot));

    menu.style.left = `${e.clientX}px`;
    menu.style.top = `${e.clientY}px`;
    document.body.appendChild(menu);
    this.menu = menu;
  }

  private closeMenu(): void {
    this.menu?.remove();
    this.menu = null;
  }
}
