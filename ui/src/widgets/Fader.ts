/** Vertical fader: aluminium cap with grip grooves riding a recessed slot.
 *  Same interaction contract as Knob (drag, Shift = fine, double-click reset,
 *  wheel, keyboard, ARIA slider).
 */

export interface FaderOptions {
  label: string;
  height?: number;          // travel in px (default 120)
  defaultValue01?: number;
  onInput?: (value01: number) => void;
  onGestureStart?: () => void;
  onGestureEnd?: () => void;
}

export class Fader {
  readonly el: HTMLElement;

  private cap: HTMLElement;
  private tooltip: HTMLElement;
  private value01 = 0;
  private text = "";
  private readonly travel: number;
  private readonly defaultValue: number;

  constructor(private opts: FaderOptions) {
    this.travel = opts.height ?? 120;
    this.defaultValue = opts.defaultValue01 ?? 0.75;

    this.el = document.createElement("div");
    this.el.className = "fader";
    this.el.style.setProperty("--fader-travel", `${this.travel}px`);
    this.el.tabIndex = 0;
    this.el.setAttribute("role", "slider");
    this.el.setAttribute("aria-label", opts.label);
    this.el.setAttribute("aria-orientation", "vertical");
    this.el.setAttribute("aria-valuemin", "0");
    this.el.setAttribute("aria-valuemax", "1");

    this.el.innerHTML = `
      <div class="fader-slot"></div>
      <div class="fader-cap"></div>
      <div class="fader-label">${opts.label}</div>
      <div class="fader-tooltip" role="status"></div>`;

    this.cap = this.el.querySelector<HTMLElement>(".fader-cap")!;
    this.tooltip = this.el.querySelector<HTMLElement>(".fader-tooltip")!;

    this.bind();
    this.render();
  }

  setValue(value01: number, text?: string): void {
    this.value01 = clamp01(value01);
    if (text !== undefined) this.text = text;
    this.render();
  }

  getValue(): number {
    return this.value01;
  }

  private bind(): void {
    let startY = 0;
    let startValue = 0;
    let dragging = false;

    this.el.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      this.el.setPointerCapture(e.pointerId);
      dragging = true;
      startY = e.clientY;
      startValue = this.value01;
      this.el.classList.add("dragging");
      this.opts.onGestureStart?.();
    });

    this.el.addEventListener("pointermove", (e) => {
      if (!dragging) return;
      const fine = e.shiftKey ? 0.1 : 1;
      this.applyInput(startValue + ((startY - e.clientY) / this.travel) * fine);
      startY = e.clientY;
      startValue = this.value01;
    });

    const finish = (e: PointerEvent) => {
      if (!dragging) return;
      dragging = false;
      this.el.classList.remove("dragging");
      this.el.releasePointerCapture(e.pointerId);
      this.opts.onGestureEnd?.();
    };
    this.el.addEventListener("pointerup", finish);
    this.el.addEventListener("pointercancel", finish);

    this.el.addEventListener("dblclick", () => {
      this.opts.onGestureStart?.();
      this.applyInput(this.defaultValue);
      this.opts.onGestureEnd?.();
    });

    this.el.addEventListener(
      "wheel",
      (e) => {
        e.preventDefault();
        const step = (e.shiftKey ? 0.002 : 0.02) * (e.deltaY < 0 ? 1 : -1);
        this.opts.onGestureStart?.();
        this.applyInput(this.value01 + step);
        this.opts.onGestureEnd?.();
      },
      { passive: false },
    );

    this.el.addEventListener("keydown", (e) => {
      const step = e.shiftKey ? 0.005 : 0.02;
      let next: number | null = null;
      if (e.key === "ArrowUp") next = this.value01 + step;
      else if (e.key === "ArrowDown") next = this.value01 - step;
      else if (e.key === "Home") next = 0;
      else if (e.key === "End") next = 1;
      if (next !== null) {
        e.preventDefault();
        this.opts.onGestureStart?.();
        this.applyInput(next);
        this.opts.onGestureEnd?.();
      }
    });
  }

  private applyInput(value01: number): void {
    this.value01 = clamp01(value01);
    this.render();
    this.opts.onInput?.(this.value01);
  }

  private render(): void {
    this.cap.style.transform = `translate(-50%, ${(1 - this.value01) * this.travel}px)`;
    this.tooltip.textContent = this.text;
    this.el.setAttribute("aria-valuenow", this.value01.toFixed(3));
    if (this.text) this.el.setAttribute("aria-valuetext", this.text);
  }
}

const clamp01 = (v: number): number => Math.min(1, Math.max(0, v));
