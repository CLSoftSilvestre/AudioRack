/** Shared faceplate plumbing: param-bound knobs and switches, brand blocks,
 *  bypass controls. Keeps each unit file focused on its layout.
 */

import { Knob, type KnobOptions } from "../widgets/Knob";
import { Switch } from "../widgets/Switch";
import { Selector } from "../widgets/Selector";
import { screw } from "../widgets/Screw";
import { openContextMenu, type MenuItem } from "../widgets/contextMenu";
import { slotParamID } from "../bridge/protocol";
import type { Store } from "../store";

export interface Bound {
  el: HTMLElement;
  unsub: () => void;
}

/** Right-click MIDI learn for a control: arm/cancel/forget, plus an armed ring
 *  and a "CCn" badge. Used for knobs and switches — selectors already use
 *  right-click to step their value, so they are intentionally excluded. */
export function attachMidiLearn(store: Store, paramId: string, el: HTMLElement): () => void {
  el.classList.add("midi-target");

  const badge = document.createElement("span");
  badge.className = "midi-badge";
  el.appendChild(badge);

  const onContext = (e: MouseEvent) => {
    e.preventDefault();
    e.stopPropagation();

    const cc = store.midiCcFor(paramId);
    const items: MenuItem[] = store.isMidiArmed(paramId)
      ? [{ label: "Cancel MIDI Learn", action: () => store.clearMidiLearn() }]
      : [{ label: "MIDI Learn", action: () => store.armMidiLearn(paramId) }];

    if (cc !== undefined) items.push({ label: `Forget MIDI CC ${cc}`, action: () => store.forgetMidi(paramId) });

    openContextMenu(e.clientX, e.clientY, items);
  };
  el.addEventListener("contextmenu", onContext);

  const unsub = store.onMidi(() => {
    const cc = store.midiCcFor(paramId);
    const armed = store.isMidiArmed(paramId);
    el.classList.toggle("midi-armed", armed);
    el.classList.toggle("midi-mapped", cc !== undefined && !armed);
    badge.textContent = armed ? "LEARN" : cc !== undefined ? `CC${cc}` : "";
  });

  return () => {
    el.removeEventListener("contextmenu", onContext);
    unsub();
    badge.remove();
  };
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
  // Show the default until the host's real value arrives, so knobs never sit
  // at the minimum during the ar_rack -> ar_params gap.
  if (opts.defaultValue01 !== undefined) knob.setValue(opts.defaultValue01);
  const unsubParam = store.onParam(paramId, (p) => knob.setValue(p.value01, p.text));
  const unsubMidi = attachMidiLearn(store, paramId, knob.el);
  return {
    el: knob.el,
    unsub: () => {
      unsubParam();
      unsubMidi();
    },
  };
}

/** Two-position toggle bound to a 2-choice parameter (0 = off-label). */
export function paramSwitch(store: Store, paramId: string, label: string): Bound {
  const sw = new Switch({
    label,
    onChange: (on) => store.setParam(paramId, on ? 1 : 0),
  });
  const unsubParam = store.onParam(paramId, (p) => sw.setOn(p.value01 >= 0.5));
  const unsubMidi = attachMidiLearn(store, paramId, sw.el);
  return {
    el: sw.el,
    unsub: () => {
      unsubParam();
      unsubMidi();
    },
  };
}

/** Stepped selector bound to an N-choice parameter (shows host text). */
export function paramSelector(store: Store, paramId: string, label: string, count: number): Bound {
  const sel = new Selector({
    label,
    count,
    onSelect: (v) => {
      store.beginGesture(paramId);
      store.setParam(paramId, v);
      store.endGesture(paramId);
    },
  });
  const unsub = store.onParam(paramId, (p) => sel.setValue(p.value01, p.text));
  return { el: sel.el, unsub };
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
