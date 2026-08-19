/** Illuminated rocker power switch — the on/off control for a rack unit.
 *
 *  Modelled on a classic panel-mount SPST illuminated rocker: a black
 *  cross-hatched bezel with a red lens that tilts on an X axis. Pressing the
 *  "I" (top) side down engages the unit — the lens rocks in at the top and
 *  glows red; the "O" (bottom) side down bypasses it and the lens goes dark.
 *  Purely CSS-rendered; exposes ARIA switch semantics like the bat Switch.
 */

export interface RockerSwitchOptions {
  label: string;
  onChange?: (on: boolean) => void;
}

export class RockerSwitch {
  readonly el: HTMLElement;

  private on = false;

  constructor(private opts: RockerSwitchOptions) {
    this.el = document.createElement("div");
    this.el.className = "rocker";
    this.el.tabIndex = 0;
    this.el.setAttribute("role", "switch");
    this.el.setAttribute("aria-label", opts.label);

    this.el.innerHTML = `
      <div class="rocker-bezel">
        <div class="rocker-face">
          <span class="rocker-glyph top">I</span>
          <span class="rocker-glyph bottom">O</span>
          <span class="rocker-gloss"></span>
        </div>
      </div>
      <div class="rocker-label">${opts.label}</div>`;

    this.el.addEventListener("click", () => this.toggle());
    this.el.addEventListener("keydown", (e) => {
      if (e.key === " " || e.key === "Enter") {
        e.preventDefault();
        this.toggle();
      }
    });

    this.render();
  }

  setOn(on: boolean): void {
    this.on = on;
    this.render();
  }

  isOn(): boolean {
    return this.on;
  }

  private toggle(): void {
    this.on = !this.on;
    this.render();
    this.opts.onChange?.(this.on);
  }

  private render(): void {
    this.el.classList.toggle("on", this.on);
    this.el.setAttribute("aria-checked", String(this.on));
  }
}
