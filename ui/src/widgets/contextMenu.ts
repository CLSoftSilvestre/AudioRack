/** One shared floating context menu, reused by the rack and by per-control
 *  menus (MIDI learn). Only one is ever open; a pointerdown outside dismisses it.
 */

export interface MenuItem {
  label: string;
  action: () => void;
}

let current: HTMLElement | null = null;

export function closeContextMenu(): void {
  current?.remove();
  current = null;
}

export function openContextMenu(x: number, y: number, items: MenuItem[]): void {
  closeContextMenu();
  if (items.length === 0) return;

  const menu = document.createElement("div");
  menu.className = "context-menu";

  for (const item of items) {
    const button = document.createElement("button");
    button.className = "context-item";
    button.textContent = item.label;
    button.addEventListener("click", () => {
      item.action();
      closeContextMenu();
    });
    menu.appendChild(button);
  }

  menu.style.left = `${x}px`;
  menu.style.top = `${y}px`;
  document.body.appendChild(menu);
  current = menu;
}

document.addEventListener("pointerdown", (e) => {
  if (current && !current.contains(e.target as Node)) closeContextMenu();
});
