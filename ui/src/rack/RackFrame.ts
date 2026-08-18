/** The 19" rack: side posts with square mounting holes, numbered slots,
 *  mounted units. Rebuilds units only when the layout actually changes;
 *  meter/param updates never touch this layer.
 *
 *  Interactions here are the pre-M7 minimum: double-click an empty slot for
 *  a module picker, right-click a unit for bypass/remove. Drag-reorder, A/B
 *  and the browser panel arrive with M7.
 */

import { GainUnit } from "../units/GainUnit";
import { CompressorUnit } from "../units/CompressorUnit";
import { LimiterUnit } from "../units/LimiterUnit";
import { GateUnit } from "../units/GateUnit";
import { MAX_SLOTS, slotParamID } from "../bridge/protocol";
import type { Store } from "../store";

interface UnitInstance {
  el: HTMLElement;
  dispose(): void;
}

const UNIT_FACTORY: Record<string, new (store: Store, slot: number) => UnitInstance> = {
  gain: GainUnit,
  comp: CompressorUnit,
  lim: LimiterUnit,
  gate: GateUnit,
};

interface Mounted {
  unit: UnitInstance;
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

      slotEl.addEventListener("dblclick", (e) => {
        if (!this.mounted.has(slot)) this.openModulePicker(e, slot);
      });

      slotEl.addEventListener("contextmenu", (e) => {
        e.preventDefault();
        if (this.mounted.has(slot)) this.openUnitMenu(e, slot);
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
        this.slotEls[slot].style.removeProperty("height");
      }

      if (wanted !== "") {
        const UnitClass = UNIT_FACTORY[wanted];
        if (!UnitClass) continue;

        const units = this.store.moduleInfo(wanted)?.units ?? 1;
        const unit = new UnitClass(this.store, slot);

        this.slotEls[slot].style.height = `calc(var(--u) * ${units})`;
        this.slotEls[slot].appendChild(unit.el);
        this.slotEls[slot].classList.remove("empty");
        this.mounted.set(slot, { unit, moduleId: wanted });
      }
    }
  }

  // --- menus --------------------------------------------------------------------

  private buildMenu(x: number, y: number, items: [string, () => void][]): void {
    this.closeMenu();

    const menu = document.createElement("div");
    menu.className = "context-menu";

    for (const [label, action] of items) {
      const item = document.createElement("button");
      item.className = "context-item";
      item.textContent = label;
      item.addEventListener("click", () => {
        action();
        this.closeMenu();
      });
      menu.appendChild(item);
    }

    menu.style.left = `${x}px`;
    menu.style.top = `${y}px`;
    document.body.appendChild(menu);
    this.menu = menu;
  }

  private openModulePicker(e: MouseEvent, slot: number): void {
    const items: [string, () => void][] = this.store
      .availableModules()
      .map((m) => [
        `${m.name}  ·  ${m.category} · ${m.units}U`,
        () => this.store.mount(slot, m.id),
      ]);

    if (items.length > 0) this.buildMenu(e.clientX, e.clientY, items);
  }

  private openUnitMenu(e: MouseEvent, slot: number): void {
    const bypassId = slotParamID(slot, "bypass");
    const bypassed = this.store.param(bypassId).value01 >= 0.5;

    this.buildMenu(e.clientX, e.clientY, [
      [bypassed ? "Enable" : "Bypass", () => this.store.setParam(bypassId, bypassed ? 0 : 1)],
      ["Remove", () => this.store.unmount(slot)],
    ]);
  }

  private closeMenu(): void {
    this.menu?.remove();
    this.menu = null;
  }
}
