/** Module browser: the palette of available modules on the left of the studio.
 *
 *  Each chip is a drag source ("new" module -> drop into an empty slot). The
 *  panel is also the "remove" drop zone: drag a mounted unit here to unmount it.
 *  Double-clicking a chip mounts it into the first free slot as a shortcut.
 */

import type { Store } from "../store";
import type { ModuleInfo } from "../bridge/protocol";
import type { DragManager } from "./dnd";

export class BrowserPanel {
  readonly el: HTMLElement;
  private list: HTMLElement;

  constructor(
    private store: Store,
    private drag: DragManager,
  ) {
    this.el = document.createElement("div");
    this.el.className = "browser";
    this.el.innerHTML = `
      <div class="browser-head">
        <div class="browser-title">MODULES</div>
        <div class="browser-sub">drag into a slot</div>
      </div>
      <div class="browser-list"></div>
      <div class="browser-trash">drag a unit here to remove</div>`;

    this.list = this.el.querySelector<HTMLElement>(".browser-list")!;

    this.drag.setRemoveZone(this.el);
    this.store.onRack((_, modules) => this.render(modules));
  }

  private render(modules: ModuleInfo[]): void {
    this.list.textContent = "";

    // Group by category, preserving registry order within each group.
    const groups = new Map<string, ModuleInfo[]>();
    for (const m of modules) {
      const g = groups.get(m.category) ?? [];
      g.push(m);
      groups.set(m.category, g);
    }

    for (const [category, mods] of groups) {
      const header = document.createElement("div");
      header.className = "browser-group";
      header.textContent = category;
      this.list.appendChild(header);

      for (const m of mods) this.list.appendChild(this.chip(m));
    }
  }

  private chip(m: ModuleInfo): HTMLElement {
    const chip = document.createElement("div");
    chip.className = "browser-chip";
    chip.innerHTML = `
      <span class="chip-name">${m.name}</span>
      <span class="chip-u">${m.units}U</span>`;
    chip.title = `${m.name} — drag into a slot, or double-click to add`;

    this.drag.attachSource(chip, () => ({ kind: "new", moduleId: m.id, label: m.name }));

    chip.addEventListener("dblclick", () => {
      const slot = this.store.firstEmptySlot();
      if (slot >= 0) this.store.mount(slot, m.id);
    });

    return chip;
  }
}
