/** "About" dialog — brand, version and credits. Opened from the toolbar
 *  wordmark. Closes on the × button, a backdrop click, or Escape.
 */

import type { Store } from "../store";

const AZORDEV_URL = "https://azordev.pt";

let current: HTMLElement | null = null;

export function openAbout(store: Store): void {
  closeAbout();

  const version = store.appVersion();

  const overlay = document.createElement("div");
  overlay.className = "about-overlay";
  overlay.innerHTML = `
    <div class="about-card" role="dialog" aria-modal="true" aria-label="About AudioRack">
      <button class="about-close" aria-label="Close">&times;</button>
      <div class="about-logo">AUDIO<span>RACK</span></div>
      <div class="about-tag">Virtual 19&Prime; effects rack</div>
      ${version ? `<div class="about-version">Version ${version}</div>` : ""}
      <div class="about-credit">
        Crafted by <a class="about-link" href="#">Azordev.pt</a>
        <span class="about-author">Celso Silvestre</span>
      </div>
      <div class="about-licence">Open source under the GNU AGPLv3.</div>
    </div>`;

  const close = () => closeAbout();

  overlay.addEventListener("pointerdown", (e) => {
    if (e.target === overlay) close(); // backdrop click only
  });
  overlay.querySelector<HTMLButtonElement>(".about-close")!.addEventListener("click", close);
  overlay.querySelector<HTMLAnchorElement>(".about-link")!.addEventListener("click", (e) => {
    e.preventDefault();
    store.openUrl(AZORDEV_URL);
  });

  document.addEventListener("keydown", onKey);
  document.body.appendChild(overlay);
  current = overlay;
}

export function closeAbout(): void {
  if (!current) return;
  document.removeEventListener("keydown", onKey);
  current.remove();
  current = null;
}

function onKey(e: KeyboardEvent): void {
  if (e.key === "Escape") closeAbout();
}
