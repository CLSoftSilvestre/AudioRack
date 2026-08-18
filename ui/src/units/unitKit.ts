/** Shared faceplate plumbing: param-bound knobs and switches, brand blocks,
 *  bypass controls. Keeps each unit file focused on its layout.
 */

import { Knob, type KnobOptions } from "../widgets/Knob";
import { Switch } from "../widgets/Switch";
import { screw } from "../widgets/Screw";
import { slotParamID } from "../bridge/protocol";
import type { Store } from "../store";

export interface Bound {
  el: HTMLElement;
  unsub: () => void;
}

/** Knob bound to a host parameter (normalized value + host display text). */
export function paramKnob(
  store: Store,
  paramId: string,
  opts: Omit<KnobOptions, "onInput" | "onGestureStart" | "onGestureEnd">,
): Bound {
  const knob = new Knob({
    ...opts,
    onInput: (v) => store.setParam(paramId, v),
    onGestureStart: () => store.beginGesture(paramId),
    onGestureEnd: () => store.endGesture(paramId),
  });
  const unsub = store.onParam(paramId, (p) => knob.setValue(p.value01, p.text));
  return { el: knob.el, unsub };
}

/** Two-position toggle bound to a 2-choice parameter (0 = off-label). */
export function paramSwitch(store: Store, paramId: string, label: string): Bound {
  const sw = new Switch({
    label,
    onChange: (on) => store.setParam(paramId, on ? 1 : 0),
  });
  const unsub = store.onParam(paramId, (p) => sw.setOn(p.value01 >= 0.5));
  return { el: sw.el, unsub };
}

/** Standard bypass bat switch + power LED; lit = processing. */
export function bypassControl(store: Store, slot: number, unitRoot: HTMLElement): Bound {
  const bypassId = slotParamID(slot, "bypass");

  const wrap = document.createElement("div");
  wrap.className = "unit-section unit-bypass-well";

  const led = document.createElement("div");
  led.className = "power-led";

  const sw = new Switch({
    label: "IN",
    onChange: (on) => store.setParam(bypassId, on ? 0 : 1),
  });

  wrap.appendChild(sw.el);
  wrap.appendChild(led);

  const unsub = store.onParam(bypassId, (p) => {
    const bypassed = p.value01 >= 0.5;
    sw.setOn(!bypassed);
    led.classList.toggle("lit", !bypassed);
    unitRoot.classList.toggle("bypassed", bypassed);
  });

  return { el: wrap, unsub };
}

export function brandBlock(model: string, series: string): HTMLElement {
  const el = document.createElement("div");
  el.className = "unit-brand";
  el.innerHTML = `
    <div class="unit-logo">AUDIO<span>RACK</span></div>
    <div class="unit-model">${model}</div>
    <div class="unit-series">${series}</div>`;
  return el;
}

export function chassis(className: string): {
  root: HTMLElement;
  face: HTMLElement;
} {
  const root = document.createElement("div");
  root.className = `unit ${className}`;
  root.innerHTML = `
    <div class="unit-ear left"></div>
    <div class="unit-face"></div>
    <div class="unit-ear right"></div>`;

  for (const side of ["left", "right"] as const) {
    const ear = root.querySelector<HTMLElement>(`.unit-ear.${side}`)!;
    ear.appendChild(screw(13));
    ear.appendChild(screw(13));
  }

  return { root, face: root.querySelector<HTMLElement>(".unit-face")! };
}
