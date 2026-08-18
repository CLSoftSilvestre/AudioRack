/** Stepped selector for discrete/choice parameters: an illuminated readout
 *  that cycles on click/wheel, with a little detented look. Bound to a
 *  normalized value that maps to N option indices.
 */

export interface SelectorOptions {
  label: string;
  count: number; // number of choices
  onSelect?: (value01: number) => void;
}

export class Selector {
  readonly el: HTMLElement;
  private readout: HTMLElement;
  private index = 0;
  private text = "";

  constructor(private opts: SelectorOptions) {
    this.el = document.createElement("div");
    this.el.className = "selector";
    this.el.tabIndex = 0;
    this.el.setAttribute("role", "listbox");
    this.el.setAttribute("aria-label", opts.label);
    this.el.innerHTML = `
      <div class="selector-window"><span class="selector-text"></span></div>
      <div class="selector-label">${opts.label}</div>`;
    this.readout = this.el.querySelector<HTMLElement>(".selector-text")!;

    this.el.addEventListener("click", () => this.step(1));
    this.el.addEventListener("contextmenu", (e) => {
      e.preventDefault();
      this.step(-1);
    });
    this.el.addEventListener(
      "wheel",
      (e) => {
        e.preventDefault();
        this.step(e.deltaY < 0 ? 1 : -1);
      },
      { passive: false },
    );
    this.el.addEventListener("keydown", (e) => {
      if (e.key === "ArrowRight" || e.key === "ArrowUp") { e.preventDefault(); this.step(1); }
      else if (e.key === "ArrowLeft" || e.key === "ArrowDown") { e.preventDefault(); this.step(-1); }
    });
  }

  /** External update from the store (value01 + host display text). */
  setValue(value01: number, text: string): void {
    this.index = Math.round(value01 * (this.opts.count - 1));
    this.text = text;
    this.render();
  }

  private step(dir: number): void {
    this.index = (this.index + dir + this.opts.count) % this.opts.count;
    const value01 = this.opts.count > 1 ? this.index / (this.opts.count - 1) : 0;
    this.render();
    this.opts.onSelect?.(value01);
  }

  private render(): void {
    this.readout.textContent = this.text;
    this.el.setAttribute("aria-activedescendant", String(this.index));
  }
}
