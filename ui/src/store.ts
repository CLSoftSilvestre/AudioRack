/** Tiny typed store: single source of truth for parameter values, rack layout
 *  and meter frames. Widgets subscribe; the bridge is the only writer for
 *  host-originated data. No widget ever touches the bridge directly.
 */

import type { Bridge } from "./bridge/juce";
import type { MeterFrameData } from "./types";
import { MAX_SLOTS, type ModuleInfo, type UiEvent } from "./bridge/protocol";

type AbListener = (bank: number) => void;

export interface ParamState {
  value01: number;
  text: string;
}

type ParamListener = (p: ParamState) => void;
type RackListener = (slots: string[], modules: ModuleInfo[]) => void;
type MeterListener = (frame: MeterFrameData) => void;

export class Store {
  private params = new Map<string, ParamState>();
  private paramListeners = new Map<string, Set<ParamListener>>();

  private slots: string[] = Array.from({ length: MAX_SLOTS }, () => "");
  private modules: ModuleInfo[] = [];
  private rackListeners = new Set<RackListener>();

  private meterListeners = new Map<number, Set<MeterListener>>();

  private bank = 0;
  private abListeners = new Set<AbListener>();

  constructor(private bridge: Bridge) {
    bridge.onParams((msg) => {
      for (const [id, value01, text] of msg.p) {
        const state = { value01, text };
        this.params.set(id, state);
        this.paramListeners.get(id)?.forEach((fn) => fn(state));
      }
    });

    bridge.onRack((msg) => {
      this.slots = msg.slots;
      this.modules = msg.modules;
      this.rackListeners.forEach((fn) => fn(this.slots, this.modules));
    });

    bridge.onMeters((msg) => {
      for (const [slot, peakL, peakR, rmsL, rmsR, grDb] of msg.m) {
        this.meterListeners
          .get(slot)
          ?.forEach((fn) => fn({ peakL, peakR, rmsL, rmsR, grDb }));
      }
    });

    bridge.onAb((msg) => {
      this.bank = msg.bank;
      this.abListeners.forEach((fn) => fn(this.bank));
    });
  }

  start(): void {
    this.bridge.send({ type: "ready" });
  }

  // --- reads ------------------------------------------------------------------

  param(id: string): ParamState {
    return this.params.get(id) ?? { value01: 0, text: "" };
  }

  rackSlots(): string[] {
    return this.slots;
  }

  moduleInfo(id: string): ModuleInfo | undefined {
    return this.modules.find((m) => m.id === id);
  }

  availableModules(): ModuleInfo[] {
    return this.modules;
  }

  activeBank(): number {
    return this.bank;
  }

  /** Index of the first empty slot, or -1 if the rack is full. */
  firstEmptySlot(): number {
    return this.slots.indexOf("");
  }

  // --- subscriptions -------------------------------------------------------------

  onParam(id: string, fn: ParamListener): () => void {
    let set = this.paramListeners.get(id);
    if (!set) this.paramListeners.set(id, (set = new Set()));
    set.add(fn);
    const existing = this.params.get(id);
    if (existing) fn(existing);
    return () => set.delete(fn);
  }

  onRack(fn: RackListener): () => void {
    this.rackListeners.add(fn);
    return () => this.rackListeners.delete(fn);
  }

  onMeters(slot: number, fn: MeterListener): () => void {
    let set = this.meterListeners.get(slot);
    if (!set) this.meterListeners.set(slot, (set = new Set()));
    set.add(fn);
    return () => set.delete(fn);
  }

  onAb(fn: AbListener): () => void {
    this.abListeners.add(fn);
    fn(this.bank);
    return () => this.abListeners.delete(fn);
  }

  // --- writes (forwarded to native; echo comes back through onParams) ---------------

  setParam(id: string, value01: number): void {
    this.send({ type: "setParam", id, value01: Math.min(1, Math.max(0, value01)) });
  }
  beginGesture(id: string): void {
    this.send({ type: "beginGesture", id });
  }
  endGesture(id: string): void {
    this.send({ type: "endGesture", id });
  }
  mount(slot: number, moduleId: string): void {
    this.send({ type: "mount", slot, moduleId });
  }
  unmount(slot: number): void {
    this.send({ type: "unmount", slot });
  }
  move(from: number, to: number): void {
    this.send({ type: "move", from, to });
  }
  duplicate(from: number, to: number): void {
    this.send({ type: "duplicate", from, to });
  }
  selectBank(bank: number): void {
    this.send({ type: "abSelect", bank });
  }
  copyBank(): void {
    this.send({ type: "abCopy" });
  }

  private send(event: UiEvent): void {
    this.bridge.send(event);
  }
}
