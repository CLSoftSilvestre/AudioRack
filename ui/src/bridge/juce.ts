/** Thin typed wrapper over JUCE 8's injected native-integration backend
 *  (window.__JUCE__.backend). Falls back to a mock backend in a plain
 *  browser so `npm run dev` works without the plugin.
 */

import type { MetersMessage, ParamsMessage, RackMessage, UiEvent } from "./protocol";
import { MAX_SLOTS } from "./protocol";

interface JuceBackend {
  addEventListener(eventId: string, fn: (payload: unknown) => void): unknown;
  emitEvent(eventId: string, payload: unknown): void;
}

declare global {
  interface Window {
    __JUCE__?: { backend: JuceBackend };
  }
}

export interface Bridge {
  readonly isMock: boolean;
  send(event: UiEvent): void;
  onRack(fn: (msg: RackMessage) => void): void;
  onParams(fn: (msg: ParamsMessage) => void): void;
  onMeters(fn: (msg: MetersMessage) => void): void;
}

class NativeBridge implements Bridge {
  readonly isMock = false;

  constructor(private backend: JuceBackend) {}

  send(event: UiEvent): void {
    this.backend.emitEvent("ar_ui", event);
  }
  onRack(fn: (msg: RackMessage) => void): void {
    this.backend.addEventListener("ar_rack", (p) => fn(p as RackMessage));
  }
  onParams(fn: (msg: ParamsMessage) => void): void {
    this.backend.addEventListener("ar_params", (p) => fn(p as ParamsMessage));
  }
  onMeters(fn: (msg: MetersMessage) => void): void {
    this.backend.addEventListener("ar_meters", (p) => fn(p as MetersMessage));
  }
}

/** Standalone-browser stand-in: one Gain mounted, params locally echoed,
 *  meters animated so the rack looks alive during UI development.
 */
class MockBridge implements Bridge {
  readonly isMock = true;

  private rackListeners: ((msg: RackMessage) => void)[] = [];
  private paramListeners: ((msg: ParamsMessage) => void)[] = [];
  private meterListeners: ((msg: MetersMessage) => void)[] = [];

  // "?full" mounts every slot (frame-budget benchmark); "?demo" mounts one
  // of each module type (visual review).
  private slots: string[] = window.location.search.includes("demo")
    ? ["gain", "comp", "lim", "gate", ...Array.from({ length: MAX_SLOTS - 4 }, () => "")]
    : Array.from({ length: MAX_SLOTS }, (_, i) =>
        window.location.search.includes("full") || i === 0 ? "gain" : "",
      );
  private params = new Map<string, number>();

  constructor() {
    setInterval(() => this.tickMeters(), 1000 / 60);
    // Benchmark hook: lets the frame-budget bench pump meter traffic
    // synchronously (see main.ts, "?bench").
    (window as unknown as Record<string, unknown>).__arPump = () => this.tickMeters();
  }

  send(event: UiEvent): void {
    switch (event.type) {
      case "ready":
        queueMicrotask(() => {
          this.emitRack();
          this.emitAllParams();
        });
        break;
      case "setParam": {
        this.params.set(event.id, event.value01);
        const msg: ParamsMessage = { p: [[event.id, event.value01, this.format(event.id)]] };
        this.paramListeners.forEach((fn) => fn(msg));
        break;
      }
      case "mount":
        this.slots[event.slot] = event.moduleId;
        this.emitRack();
        break;
      case "unmount":
        this.slots[event.slot] = "";
        this.emitRack();
        break;
      case "move": {
        const moving = this.slots[event.from];
        this.slots[event.from] = this.slots[event.to];
        this.slots[event.to] = moving;
        this.emitRack();
        break;
      }
      case "beginGesture":
      case "endGesture":
        break;
    }
  }

  onRack(fn: (msg: RackMessage) => void): void {
    this.rackListeners.push(fn);
  }
  onParams(fn: (msg: ParamsMessage) => void): void {
    this.paramListeners.push(fn);
  }
  onMeters(fn: (msg: MetersMessage) => void): void {
    this.meterListeners.push(fn);
  }

  private value01(id: string): number {
    const v = this.params.get(id);
    if (v !== undefined) return v;
    // Defaults mirror the native layout: gain centred, mix full, bypass off.
    if (id.endsWith(".mix")) return 1;
    if (id.endsWith(".bypass")) return 0;
    return 0.5;
  }

  private format(id: string): string {
    const v = this.value01(id);
    if (id.endsWith(".bypass")) return v >= 0.5 ? "On" : "Off";
    if (id.endsWith(".mix")) return `${Math.round(v * 100)} %`;
    if (id.endsWith(".gaindb")) {
      const db = gainDbFrom01(v);
      return db <= -60 ? "-inf dB" : `${db.toFixed(1)} dB`;
    }
    return v.toFixed(2);
  }

  private emitRack(): void {
    const msg: RackMessage = {
      slots: [...this.slots],
      modules: [
        { id: "gain", name: "Gain", category: "Utility", units: 1 },
        { id: "comp", name: "Compressor", category: "Dynamics", units: 2 },
        { id: "lim", name: "Limiter", category: "Dynamics", units: 1 },
        { id: "gate", name: "Gate", category: "Dynamics", units: 1 },
      ],
    };
    this.rackListeners.forEach((fn) => fn(msg));
  }

  private emitAllParams(): void {
    const p: ParamsMessage["p"] = [];
    for (let slot = 0; slot < MAX_SLOTS; slot++) {
      for (const id of [`slot${slot}.bypass`, `slot${slot}.mix`, `slot${slot}.gain.gaindb`]) {
        p.push([id, this.value01(id), this.format(id)]);
      }
    }
    this.paramListeners.forEach((fn) => fn({ p }));
  }

  private phase = 0;

  private tickMeters(): void {
    this.phase += 1 / 60;
    const m: MetersMessage["m"] = [];

    for (let slot = 0; slot < MAX_SLOTS; slot++) {
      const moduleId = this.slots[slot];
      if (moduleId === "") continue;
      const gain = gainLinearFrom01(this.value01(`slot${slot}.gain.gaindb`));
      const bypassed = this.value01(`slot${slot}.bypass`) >= 0.5;
      const music =
        0.35 +
        0.3 * Math.sin(this.phase * 2.1 + slot) +
        0.2 * Math.sin(this.phase * 7.7) +
        0.1 * Math.random();
      const level = bypassed ? 0 : Math.max(0, music) * gain;
      // Dynamics modules show a plausible pumping GR in browser preview.
      const gr =
        moduleId !== "gain" && !bypassed
          ? Math.max(0, 5 + 5 * Math.sin(this.phase * 2.5 + slot * 1.3))
          : 0;
      m.push([slot, level * 1.25, level * 1.18, level * 0.72, level * 0.7, gr]);
    }

    if (m.length > 0) this.meterListeners.forEach((fn) => fn({ m }));
  }
}

/** Mirrors the native skewed range: -60..+12 dB, centre 0 dB. */
export function gainDbFrom01(v01: number): number {
  const min = -60, max = 12;
  const skew = Math.log(0.5) / Math.log((0 - min) / (max - min));
  return min + (max - min) * Math.pow(v01, 1 / skew);
}

export function gainDbTo01(db: number): number {
  const min = -60, max = 12;
  const skew = Math.log(0.5) / Math.log((0 - min) / (max - min));
  return Math.pow((db - min) / (max - min), skew);
}

export function gainLinearFrom01(v01: number): number {
  const db = gainDbFrom01(v01);
  return db <= -60 ? 0 : Math.pow(10, db / 20);
}

export function createBridge(): Bridge {
  const juce = window.__JUCE__;
  return juce !== undefined ? new NativeBridge(juce.backend) : new MockBridge();
}
