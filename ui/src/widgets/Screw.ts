/** Recessed Phillips screw. Each instance gets a random driver rotation so a
 *  faceplate never looks machine-perfect.
 */

export function screw(size = 12): HTMLElement {
  const el = document.createElement("div");
  el.className = "screw";
  el.style.setProperty("--screw-size", `${size}px`);
  el.style.setProperty("--screw-angle", `${Math.floor(Math.random() * 90)}deg`);
  el.setAttribute("aria-hidden", "true");
  el.innerHTML = `<div class="screw-head"><div class="screw-cross"></div></div>`;
  return el;
}
