/** Backlit LCD value display: dark glass window, glowing characters,
 *  optional unit legend. Used for numeric readouts on faceplates.
 */

export class Display {
  readonly el: HTMLElement;

  private valueEl: HTMLElement;

  constructor(label?: string, color: "green" | "amber" = "amber") {
    this.el = document.createElement("div");
    this.el.className = `display ${color}`;

    this.el.innerHTML = `
      <div class="display-window"><span class="display-value"></span></div>
      ${label ? `<div class="display-label">${label}</div>` : ""}`;

    this.valueEl = this.el.querySelector<HTMLElement>(".display-value")!;
  }

  setText(text: string): void {
    this.valueEl.textContent = text;
  }
}
