/** Pointer-based drag & drop for the rack.
 *
 *  JUCE's WebBrowserComponent is WKWebView (macOS) / WebView2 (Windows); native
 *  HTML5 drag-and-drop is unreliable inside embedded webviews, so we drive
 *  everything from pointer events instead. That also gives us a photoreal drag
 *  "ghost" and precise drop highlighting for free.
 *
 *  Two source kinds:
 *    - "new"  : a module chip from the browser panel  -> mount into an empty slot
 *    - "move" : a mounted unit (grabbed by its rack ears) -> reorder / swap, or
 *               drop onto the browser panel to remove it.
 *
 *  Hit-testing uses document.elementsFromPoint (transform/scale-safe), so it
 *  keeps working regardless of the CSS scale main.ts applies to the stage.
 */

import type { Store } from "../store";

export type DragSource =
  | { kind: "new"; moduleId: string; label: string }
  | { kind: "move"; from: number; label: string };

type DropTarget = { type: "slot"; slot: number } | { type: "remove" };

const MOVE_THRESHOLD = 5; // px before a press becomes a drag

export class DragManager {
  private slotEls: HTMLElement[] = [];
  private removeZone: HTMLElement | null = null;

  private source: DragSource | null = null;
  private ghost: HTMLElement | null = null;
  private target: DropTarget | null = null;
  private startX = 0;
  private startY = 0;
  private dragging = false;

  constructor(private store: Store) {}

  /** RackFrame registers its 12 slot elements (index === slot number). */
  setSlots(slotEls: HTMLElement[]): void {
    this.slotEls = slotEls;
  }

  /** BrowserPanel registers itself as the "drop here to remove" zone. */
  setRemoveZone(el: HTMLElement): void {
    this.removeZone = el;
  }

  /** Make `el` initiate a drag whose payload is produced lazily on press. */
  attachSource(el: HTMLElement, factory: () => DragSource): void {
    el.addEventListener("pointerdown", (e) => this.begin(e, el, factory));
  }

  private begin(e: PointerEvent, el: HTMLElement, factory: () => DragSource): void {
    if (e.button !== 0) return;
    e.preventDefault();

    this.source = factory();
    this.startX = e.clientX;
    this.startY = e.clientY;
    this.dragging = false;

    el.setPointerCapture(e.pointerId);

    const move = (ev: PointerEvent) => this.onMove(ev);
    const finish = (ev: PointerEvent) => {
      this.onUp(ev);
      el.releasePointerCapture(e.pointerId);
      el.removeEventListener("pointermove", move);
      el.removeEventListener("pointerup", finish);
      el.removeEventListener("pointercancel", cancel);
      window.removeEventListener("keydown", onKey);
    };
    const cancel = (ev: PointerEvent) => {
      this.abort();
      finish(ev);
    };
    const onKey = (ev: KeyboardEvent) => {
      if (ev.key === "Escape") this.abort();
    };

    el.addEventListener("pointermove", move);
    el.addEventListener("pointerup", finish);
    el.addEventListener("pointercancel", cancel);
    window.addEventListener("keydown", onKey);
  }

  private onMove(e: PointerEvent): void {
    if (!this.source) return;

    if (!this.dragging) {
      if (Math.hypot(e.clientX - this.startX, e.clientY - this.startY) < MOVE_THRESHOLD) return;
      this.startGhost();
    }

    this.positionGhost(e.clientX, e.clientY);
    this.updateTarget(e.clientX, e.clientY);
  }

  private onUp(e: PointerEvent): void {
    if (this.dragging) {
      this.updateTarget(e.clientX, e.clientY);
      this.drop();
    }
    this.abort();
  }

  // --- ghost ------------------------------------------------------------------

  private startGhost(): void {
    this.dragging = true;
    document.body.classList.add("dragging");

    const ghost = document.createElement("div");
    ghost.className = "drag-ghost";
    ghost.textContent = this.source?.label ?? "";
    document.body.appendChild(ghost);
    this.ghost = ghost;
  }

  private positionGhost(x: number, y: number): void {
    if (this.ghost) this.ghost.style.transform = `translate(${x + 14}px, ${y + 12}px)`;
  }

  // --- targeting --------------------------------------------------------------

  private updateTarget(x: number, y: number): void {
    this.clearHighlight();
    this.target = null;
    if (!this.source) return;

    const hit = this.hitTest(x, y);

    if (hit?.type === "slot") {
      const occupied = this.store.rackSlots()[hit.slot] !== "";
      const valid =
        this.source.kind === "new" ? !occupied : this.source.from !== hit.slot;

      this.slotEls[hit.slot]?.classList.add(valid ? "drop-ok" : "drop-bad");
      if (valid) this.target = hit;
    } else if (hit?.type === "remove" && this.source.kind === "move") {
      this.removeZone?.classList.add("drop-remove");
      this.target = hit;
    }
  }

  private hitTest(x: number, y: number): DropTarget | null {
    for (const el of document.elementsFromPoint(x, y)) {
      if (!(el instanceof HTMLElement)) continue;
      if (el.dataset.slot !== undefined) return { type: "slot", slot: Number(el.dataset.slot) };
      if (this.removeZone && (el === this.removeZone || this.removeZone.contains(el)))
        return { type: "remove" };
    }
    return null;
  }

  private drop(): void {
    if (!this.target || !this.source) return;

    if (this.target.type === "slot") {
      if (this.source.kind === "new") this.store.mount(this.target.slot, this.source.moduleId);
      else this.store.move(this.source.from, this.target.slot);
    } else if (this.target.type === "remove" && this.source.kind === "move") {
      this.store.unmount(this.source.from);
    }
  }

  // --- teardown ---------------------------------------------------------------

  private clearHighlight(): void {
    for (const el of this.slotEls) el.classList.remove("drop-ok", "drop-bad");
    this.removeZone?.classList.remove("drop-remove");
  }

  private abort(): void {
    this.clearHighlight();
    this.ghost?.remove();
    this.ghost = null;
    this.target = null;
    this.source = null;
    this.dragging = false;
    document.body.classList.remove("dragging");
  }
}
