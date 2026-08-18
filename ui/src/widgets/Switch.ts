/** Bat-handle toggle switch on a hex-nut collar. Click or Space/Enter to
 *  flip; exposes ARIA switch semantics. Purely CSS-rendered.
 */

export interface SwitchOptions {
  label: string;
  onChange?: (on: boolean) => void;
}

export class Switch {
  readonly el: HTMLElement;

  private on = false;

  constructor(private opts: SwitchOptions) {
    this.el = document.createElement("div");
    this.el.className = "switch";
    this.el.tabIndex = 0;
    this.el.setAttribute("role", "switch");
    this.el.setAttribute("aria-label", opts.label);

    this.el.innerHTML = `
      <div class="switch-collar">
        <div class="switch-bat"></div>
      </div>
      <div class="switch-label">${opts.label}</div>`;

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
