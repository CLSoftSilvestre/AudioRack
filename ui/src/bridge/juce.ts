/** Thin typed wrapper over JUCE 8's injected native-integration backend
 *  (window.__JUCE__.backend). Falls back to a mock backend in a plain
 *  browser so `npm run dev` works without the plugin.
 */

import type {
  AbMessage,
  MetersMessage,
  MidiMessage,
  ParamsMessage,
  RackMessage,
  SpectrumMessage,
  UiEvent,
} from "./protocol";
import { MAX_SLOTS, SPECTRUM_BANDS, SPECTRUM_FLOOR_DB, spectrumBandHz } from "./protocol";
import { PREVIEW_SCHEMA } from "./previewSchema";

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
  onSpectrum(fn: (msg: SpectrumMessage) => void): void;
  onAb(fn: (msg: AbMessage) => void): void;
  onMidi(fn: (msg: MidiMessage) => void): void;
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
  onSpectrum(fn: (msg: SpectrumMessage) => void): void {
    this.backend.addEventListener("ar_spectrum", (p) => fn(p as SpectrumMessage));
  }
  onAb(fn: (msg: AbMessage) => void): void {
    this.backend.addEventListener("ar_ab", (p) => fn(p as AbMessage));
  }
  onMidi(fn: (msg: MidiMessage) => void): void {
    this.backend.addEventListener("ar_midi", (p) => fn(p as MidiMessage));
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
  private spectrumListeners: ((msg: SpectrumMessage) => void)[] = [];
  private abListeners: ((msg: AbMessage) => void)[] = [];
  private midiListeners: ((msg: MidiMessage) => void)[] = [];

  private banks: [Map<string, number>, Map<string, number>] = [new Map(), new Map()];
  private activeBank = 0;

  // Browser preview has no real MIDI hardware, so arming "learns" a synthetic
  // CC after a short delay to demonstrate the flow.
  private midiArmed: string | null = null;
  private midiMap = new Map<string, number>();
  private nextCc = 20;

  // "?full" mounts every slot (frame-budget benchmark); "?eqfull" fills the
  // rack with EQ-6 instead, the analyser's worst case (12 live RTAs);
  // "?demo" mounts one of each module type (visual review).
  private slots: string[] = window.location.search.includes("demo")
    ? ["comp", "amp", "eq", "sat", "delay", "reverb", "lim", ...Array.from({ length: MAX_SLOTS - 7 }, () => "")]
    : window.location.search.includes("eqfull")
      ? Array.from({ length: MAX_SLOTS }, () => "eq")
      : Array.from({ length: MAX_SLOTS }, (_, i) =>
          window.location.search.includes("full") || i === 0 ? "gain" : "",
        );
  private params = new Map<string, number>();

  constructor() {
    setInterval(() => this.tickMeters(), 1000 / 60);
    setInterval(() => this.tickSpectrum(), 1000 / 30);
    // Benchmark hook: lets the frame-budget bench pump meter and analyser
    // traffic synchronously (see main.ts, "?bench").
    (window as unknown as Record<string, unknown>).__arPump = () => {
      this.tickMeters();
      this.tickSpectrum();
    };
  }

  send(event: UiEvent): void {
    switch (event.type) {
      case "ready":
        queueMicrotask(() => {
          this.emitRack();
          this.emitAb();
          this.emitMidi();
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
        queueMicrotask(() => this.emitAllParams());
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
      case "duplicate":
        this.slots[event.to] = this.slots[event.from];
        this.emitRack();
        queueMicrotask(() => this.emitAllParams());
        break;
      case "abSelect": {
        this.captureBank(this.activeBank);
        this.activeBank = event.bank;
        this.banks[this.activeBank].forEach((v, id) => this.params.set(id, v));
        this.emitAb();
        this.emitAllParams();
        break;
      }
      case "abCopy":
        this.captureBank(this.activeBank);
        this.banks[1 - this.activeBank] = new Map(this.banks[this.activeBank]);
        this.emitAb();
        break;
      case "midiLearn": {
        this.midiArmed = event.id;
        this.emitMidi();
        const armedId = event.id;
        window.setTimeout(() => {
          if (this.midiArmed !== armedId) return; // cancelled/re-armed meanwhile
          this.midiMap.set(armedId, this.nextCc++);
          this.midiArmed = null;
          this.emitMidi();
        }, 1200);
        break;
      }
      case "midiClearLearn":
        this.midiArmed = null;
        this.emitMidi();
        break;
      case "midiForget":
        this.midiMap.delete(event.id);
        this.emitMidi();
        break;
      case "openUrl":
        window.open(event.url, "_blank", "noopener");
        break;
      case "beginGesture":
      case "endGesture":
        break;
    }
  }

  private emitMidi(): void {
    const msg: MidiMessage = {
      armed: this.midiArmed,
      map: [...this.midiMap.entries()],
    };
    this.midiListeners.forEach((fn) => fn(msg));
  }

  private captureBank(bank: number): void {
    this.banks[bank] = new Map(this.params);
  }

  private emitAb(): void {
    const msg: AbMessage = { bank: this.activeBank };
    this.abListeners.forEach((fn) => fn(msg));
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
  onSpectrum(fn: (msg: SpectrumMessage) => void): void {
    this.spectrumListeners.push(fn);
  }
  onAb(fn: (msg: AbMessage) => void): void {
    this.abListeners.push(fn);
  }
  onMidi(fn: (msg: MidiMessage) => void): void {
    this.midiListeners.push(fn);
  }

  private schemaFor(id: string): { module: string; suffix: string } | null {
    const parts = id.split(".");
    if (parts.length === 3) return { module: parts[1], suffix: parts[2] };
    return null; // slotN.bypass / slotN.mix
  }

  private value01(id: string): number {
    const v = this.params.get(id);
    if (v !== undefined) return v;
    if (id.endsWith(".bypass")) return 0;
    if (id.endsWith(".mix")) return 1; // per-slot wet/dry
    const s = this.schemaFor(id);
    const def = s && PREVIEW_SCHEMA[s.module]?.find((p) => p.suffix === s.suffix)?.default01;
    return def ?? 0.5;
  }

  private format(id: string): string {
    const v = this.value01(id);
    if (id.endsWith(".bypass")) return v >= 0.5 ? "On" : "Off";
    if (id.endsWith(".mix")) return `${Math.round(v * 100)} %`;
    const s = this.schemaFor(id);
    const fmt = s && PREVIEW_SCHEMA[s.module]?.find((p) => p.suffix === s.suffix)?.format;
    return fmt ? fmt(v) : v.toFixed(2);
  }

  private emitRack(): void {
    const msg: RackMessage = {
      slots: [...this.slots],
      version: "0.1.0",
      modules: [
        { id: "gain", name: "Gain", category: "Utility", units: 1 },
        { id: "comp", name: "Compressor", category: "Dynamics", units: 2 },
        { id: "gate", name: "Gate", category: "Dynamics", units: 1 },
        { id: "eq", name: "Parametric EQ", category: "EQ", units: 3 },
        { id: "sat", name: "Saturator", category: "Tone", units: 1 },
        { id: "amp", name: "Guitar Amp", category: "Amp", units: 3 },
        { id: "delay", name: "Delay", category: "Time", units: 2 },
        { id: "reverb", name: "Reverb", category: "Time", units: 2 },
        { id: "lim", name: "Limiter", category: "Dynamics", units: 1 },
      ],
    };
    this.rackListeners.forEach((fn) => fn(msg));
  }

  private emitAllParams(): void {
    const p: ParamsMessage["p"] = [];
    for (let slot = 0; slot < MAX_SLOTS; slot++) {
      p.push([`slot${slot}.bypass`, this.value01(`slot${slot}.bypass`), this.format(`slot${slot}.bypass`)]);
      p.push([`slot${slot}.mix`, this.value01(`slot${slot}.mix`), this.format(`slot${slot}.mix`)]);

      const moduleId = this.slots[slot];
      const schema = PREVIEW_SCHEMA[moduleId];
      if (!schema) continue;
      for (const param of schema) {
        const id = `slot${slot}.${moduleId}.${param.suffix}`;
        p.push([id, this.value01(id), this.format(id)]);
      }
    }
    this.paramListeners.forEach((fn) => fn({ p }));
  }

  private spectrumPhase = 0;
  private spectrumDb = new Float32Array(SPECTRUM_BANDS).fill(SPECTRUM_FLOOR_DB);

  /** Browser preview: a pink-ish noise floor with a couple of wandering
   *  resonances, run through the same fast-attack / slow-release ballistics as
   *  the native analyser so the widget is developed against realistic motion.
   */
  private tickSpectrum(): void {
    this.spectrumPhase += 1 / 30;
    const s: number[][] = [];

    for (let slot = 0; slot < MAX_SLOTS; slot++) {
      if (this.slots[slot] !== "eq") continue;
      const bypassed = this.value01(`slot${slot}.bypass`) >= 0.5;

      const peak1 = 200 * Math.pow(4, 1 + Math.sin(this.spectrumPhase * 0.7));
      const peak2 = 90 * Math.pow(3, 1 + Math.sin(this.spectrumPhase * 1.1 + 2));
      const entry: number[] = [slot];

      for (let b = 0; b < SPECTRUM_BANDS; b++) {
        const hz = spectrumBandHz(b);
        // -4.5 dB/octave tilt, the rough long-term slope of mixed music.
        let db = -18 - 4.5 * Math.log2(hz / 100) + 4 * Math.random();
        db += 22 / (1 + Math.pow((Math.log2(hz / peak1) * 3), 2));
        db += 16 / (1 + Math.pow((Math.log2(hz / peak2) * 4), 2));
        if (bypassed) db = SPECTRUM_FLOOR_DB;

        const prev = this.spectrumDb[b];
        this.spectrumDb[b] = db > prev ? db : prev + (db - prev) * 0.3;
        entry.push(Math.round(Math.max(SPECTRUM_FLOOR_DB, this.spectrumDb[b]) * 2));
      }
      s.push(entry);
    }

    if (s.length > 0) this.spectrumListeners.forEach((fn) => fn({ s }));
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
