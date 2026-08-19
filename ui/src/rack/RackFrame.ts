/** The 19" rack: side posts with square mounting holes, numbered slots,
 *  mounted units. Rebuilds units only when the layout actually changes;
 *  meter/param updates never touch this layer.
 *
 *  Interactions: double-click an empty slot for a module picker; right-click a
 *  unit for bypass / duplicate / remove; grab a unit by its rack ears to
 *  drag-reorder (swap) or drag it onto the browser panel to remove; drop a
 *  module chip from the browser onto an empty slot to mount it.
 */

import { GainUnit } from "../units/GainUnit";
import { CompressorUnit } from "../units/CompressorUnit";
import { LimiterUnit } from "../units/LimiterUnit";
import { GateUnit } from "../units/GateUnit";
import { EqUnit } from "../units/EqUnit";
import { SaturatorUnit } from "../units/SaturatorUnit";
import { AmpUnit } from "../units/AmpUnit";
import { DelayUnit } from "../units/DelayUnit";
import { ReverbUnit } from "../units/ReverbUnit";
import { MAX_SLOTS, slotParamID } from "../bridge/protocol";
import { openContextMenu, type MenuItem } from "../widgets/contextMenu";
import type { Store } from "../store";
import type { DragManager } from "./dnd";

interface UnitInstance {
  el: HTMLElement;
  dispose(): void;
}

const UNIT_FACTORY: Record<string, new (store: Store, slot: number) => UnitInstance> = {
  gain: GainUnit,
  comp: CompressorUnit,
  lim: LimiterUnit,
  gate: GateUnit,
  eq: EqUnit,
  sat: SaturatorUnit,
  amp: AmpUnit,
  delay: DelayUnit,
  reverb: ReverbUnit,
};

interface Mounted {
  unit: UnitInstance;
  moduleId: string;
}

export class RackFrame {
  readonly el: HTMLElement;

  private slotEls: HTMLElement[] = [];
  private mounted = new Map<number, Mounted>();

  constructor(
    private store: Store,
    private drag: DragManager,
  ) {
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

    this.drag.setSlots(this.slotEls);

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

        const info = this.store.moduleInfo(wanted);
        const units = info?.units ?? 1;
        const unit = new UnitClass(this.store, slot);

        this.slotEls[slot].style.height = `calc(var(--u) * ${units})`;
        this.slotEls[slot].appendChild(unit.el);
        this.slotEls[slot].classList.remove("empty");
        this.mounted.set(slot, { unit, moduleId: wanted });

        // The rack ears are the drag handle — grab a unit there to move it.
        const label = info?.name ?? wanted;
        for (const ear of unit.el.querySelectorAll<HTMLElement>(".unit-ear")) {
          ear.classList.add("drag-handle");
          ear.title = "drag to move · drop on the browser to remove";
          this.drag.attachSource(ear, () => ({ kind: "move", from: slot, label }));
        }
      }
    }
  }

  // --- menus --------------------------------------------------------------------

  private openModulePicker(e: MouseEvent, slot: number): void {
    const items: MenuItem[] = this.store.availableModules().map((m) => ({
      label: `${m.name}  ·  ${m.category} · ${m.units}U`,
      action: () => this.store.mount(slot, m.id),
    }));

    openContextMenu(e.clientX, e.clientY, items);
  }

  private openUnitMenu(e: MouseEvent, slot: number): void {
    const bypassId = slotParamID(slot, "bypass");
    const bypassed = this.store.param(bypassId).value01 >= 0.5;

    const items: MenuItem[] = [
      {
        label: bypassed ? "Enable" : "Bypass",
        action: () => this.store.setParam(bypassId, bypassed ? 0 : 1),
      },
    ];

    const target = this.store.firstEmptySlot();
    if (target >= 0) items.push({ label: "Duplicate", action: () => this.store.duplicate(slot, target) });

    items.push({ label: "Remove", action: () => this.store.unmount(slot) });

    openContextMenu(e.clientX, e.clientY, items);
  }
}
