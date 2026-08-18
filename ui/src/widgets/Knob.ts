/** Rotary knob with hardware behaviour:
 *    - vertical drag (pointer capture), Shift = fine (x0.1)
 *    - double-click resets to default
 *    - mouse wheel (Shift = fine), arrow keys / Home / End
 *    - value tooltip while hovering or dragging
 *    - ARIA slider semantics
 *  Rendering is pure CSS transforms on a pre-built DOM; nothing re-layouts
 *  during a drag. Travel is 270deg (-135..+135).
 */

export interface KnobOptions {
  label: string;
  size?: number;            // outer diameter in px (default 56)
  defaultValue01?: number;
  ticks?: string[];         // evenly spread scale legends, first = min
  onInput?: (value01: number) => void;
  onGestureStart?: () => void;
  onGestureEnd?: () => void;
}

const TRAVEL = 270;

export class Knob {
  readonly el: HTMLElement;

  private rotor: HTMLElement;
  private tooltip: HTMLElement;
  private value01 = 0;
  private text = "";
  private readonly defaultValue: number;

  constructor(private opts: KnobOptions) {
    this.defaultValue = opts.defaultValue01 ?? 0.5;
    const size = opts.size ?? 56;

    this.el = document.createElement("div");
    this.el.className = "knob";
    this.el.style.setProperty("--knob-size", `${size}px`);
    this.el.tabIndex = 0;
    this.el.setAttribute("role", "slider");
    this.el.setAttribute("aria-label", opts.label);
    this.el.setAttribute("aria-valuemin", "0");
    this.el.setAttribute("aria-valuemax", "1");

    this.el.innerHTML = `
      ${this.scaleSvg(size)}
      <div class="knob-body">
        <div class="knob-rotor">
          <div class="knob-knurl"></div>
          <div class="knob-cap"></div>
          <div class="knob-pointer"></div>
        </div>
        <div class="knob-gloss"></div>
      </div>
      <div class="knob-label">${opts.label}</div>
      <div class="knob-tooltip" role="status"></div>`;

    this.rotor = this.el.querySelector<HTMLElement>(".knob-rotor")!;
    this.tooltip = this.el.querySelector<HTMLElement>(".knob-tooltip")!;

    this.bindPointer();
    this.bindWheel();
    this.bindKeyboard();

    this.render();
  }

  /** External (host/store) update: display only, no events fired back. */
  setValue(value01: number, text?: string): void {
    this.value01 = clamp01(value01);
    if (text !== undefined) this.text = text;
    this.render();
  }

  getValue(): number {
    return this.value01;
  }

  // --- interaction -------------------------------------------------------------

  private bindPointer(): void {
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
      const delta = ((startY - e.clientY) / 200) * fine;
      this.applyInput(startValue + delta);
      // Re-anchor so toggling Shift mid-drag doesn't jump.
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
  }

  private bindWheel(): void {
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
  }

  private bindKeyboard(): void {
    this.el.addEventListener("keydown", (e) => {
      const step = e.shiftKey ? 0.005 : 0.02;
      let next: number | null = null;

      if (e.key === "ArrowUp" || e.key === "ArrowRight") next = this.value01 + step;
      else if (e.key === "ArrowDown" || e.key === "ArrowLeft") next = this.value01 - step;
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

  // --- rendering ------------------------------------------------------------------

  private render(): void {
    const angle = -TRAVEL / 2 + this.value01 * TRAVEL;
    this.rotor.style.transform = `rotate(${angle}deg)`;
    this.tooltip.textContent = this.text;
    this.el.setAttribute("aria-valuenow", this.value01.toFixed(3));
    if (this.text) this.el.setAttribute("aria-valuetext", this.text);
  }

  private scaleSvg(size: number): string {
    const r = size / 2 + 7;
    const c = r + 2;
    const box = c * 2;
    let marks = "";

    for (let i = 0; i <= 10; i++) {
      const major = i % 5 === 0;
      const a = ((-TRAVEL / 2 + (i / 10) * TRAVEL - 90) * Math.PI) / 180;
      const r1 = r - (major ? 5 : 3);
      marks += `<line x1="${c + r1 * Math.cos(a)}" y1="${c + r1 * Math.sin(a)}"
                      x2="${c + r * Math.cos(a)}"  y2="${c + r * Math.sin(a)}"
                      class="${major ? "tick-major" : "tick-minor"}"/>`;
    }

    const legends = this.opts.ticks ?? [];
    let labels = "";
    legends.forEach((legend, i) => {
      const t = legends.length === 1 ? 0.5 : i / (legends.length - 1);
      const a = ((-TRAVEL / 2 + t * TRAVEL - 90) * Math.PI) / 180;
      const lr = r + 6;
      labels += `<text x="${c + lr * Math.cos(a)}" y="${c + lr * Math.sin(a) + 2}"
                       text-anchor="middle" class="tick-text">${legend}</text>`;
    });

    return `<svg class="knob-scale" width="${box}" height="${box}"
                 viewBox="0 0 ${box} ${box}" aria-hidden="true">${marks}${labels}</svg>`;
  }
}

const clamp01 = (v: number): number => Math.min(1, Math.max(0, v));
