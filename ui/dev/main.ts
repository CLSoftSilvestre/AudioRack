/** Storybook-style widget gallery: `npm run dev`, then open /dev/.
 *  Every widget runs against a synthetic signal so ballistics, decay and
 *  interaction can be tuned without launching a host.
 */

import "../src/styles.css";

import { Knob } from "../src/widgets/Knob";
import { Fader } from "../src/widgets/Fader";
import { VuMeter } from "../src/widgets/VuMeter";
import { LedLadder } from "../src/widgets/LedLadder";
import { Switch } from "../src/widgets/Switch";
import { Display } from "../src/widgets/Display";
import { screw } from "../src/widgets/Screw";

const root = document.getElementById("gallery")!;
document.body.className = "studio";
root.className = "gallery";
root.innerHTML = `<h1>AUDIORACK — WIDGET GALLERY</h1><div class="gallery-grid"></div>`;
const grid = root.querySelector<HTMLElement>(".gallery-grid")!;

function cell(title: string, dark = false): HTMLElement {
  const el = document.createElement("div");
  el.className = `gallery-cell${dark ? " dark" : ""}`;
  el.innerHTML = `<h2>${title}</h2>`;
  grid.appendChild(el);
  return el;
}

// --- Knobs ---------------------------------------------------------------------

const knobCell = cell("Knob — drag / shift / dblclick / wheel / keys");
knobCell.style.display = "flex";
knobCell.style.gap = "18px";

const fmt = (v: number) => `${(v * 72 - 60).toFixed(1)} dB`;

for (const [label, size] of [["GAIN", 62], ["DRIVE", 52], ["TRIM", 40]] as const) {
  const knob = new Knob({
    label,
    size,
    defaultValue01: 0.5,
    ticks: ["-60", "-30", "0", "+12"],
    onInput: (v) => knob.setValue(v, fmt(v)),
  });
  knob.setValue(0.5, fmt(0.5));
  knobCell.appendChild(knob.el);
}

// --- Fader ---------------------------------------------------------------------

const faderCell = cell("Fader");
const fader = new Fader({
  label: "LEVEL",
  height: 120,
  defaultValue01: 0.75,
  onInput: (v) => fader.setValue(v, `${(v * 100).toFixed(0)} %`),
});
fader.setValue(0.75, "75 %");
faderCell.appendChild(fader.el);

// --- Signal source for the meters ------------------------------------------------

let phase = 0;
let signal = 0;
setInterval(() => {
  phase += 1 / 60;
  const burst = Math.sin(phase * 0.7) > 0.55 ? 1.35 : 1.0;
  signal =
    Math.max(
      0,
      0.4 + 0.3 * Math.sin(phase * 2.2) + 0.22 * Math.sin(phase * 9.1) + 0.08 * Math.random(),
    ) * burst;
}, 1000 / 60);

// --- VU -----------------------------------------------------------------------

const vuCell = cell("VU meter — 300 ms ballistics, peak LED");
const vu = new VuMeter();
vuCell.appendChild(vu.el);
setInterval(() => vu.setLevel(signal * 0.8), 1000 / 30);

// --- LED ladder -------------------------------------------------------------------

const ladderCell = cell("LED ladder — hold-peak, dB spacing", true);
const ladder = new LedLadder(2);
ladderCell.appendChild(ladder.el);
setInterval(() => ladder.setLevels(signal, signal * 0.92), 1000 / 30);

// --- Switches, LEDs, screws ---------------------------------------------------------

const hwCell = cell("Switch · LED · screws");
hwCell.style.display = "flex";
hwCell.style.alignItems = "center";
hwCell.style.gap = "20px";

const led = document.createElement("div");
led.className = "power-led lit";

const toggle = new Switch({ label: "IN", onChange: (on) => led.classList.toggle("lit", on) });
toggle.setOn(true);

hwCell.appendChild(toggle.el);
hwCell.appendChild(led);
for (const s of [10, 12, 14]) hwCell.appendChild(screw(s));

// --- Display --------------------------------------------------------------------------

const dispCell = cell("Display");
dispCell.style.display = "flex";
dispCell.style.gap = "16px";

const dispA = new Display("OUTPUT", "amber");
const dispG = new Display("TIME", "green");
dispCell.appendChild(dispA.el);
dispCell.appendChild(dispG.el);
setInterval(() => {
  dispA.setText(`${(20 * Math.log10(Math.max(1e-3, signal))).toFixed(1)} dB`);
  dispG.setText(`${(375 + 125 * Math.sin(phase * 0.5)).toFixed(0)} ms`);
}, 150);
