/** Live EQ magnitude-response display with a real-time analyser behind it.
 *
 *  Computes |H(e^jω)| for each active band from the same RBJ biquad
 *  coefficients as the C++ (src/dsp/common/Biquad.h) and sums the log
 *  magnitudes: dB grid, per-band coloured curves and the summed response, plus
 *  draggable band handles. Behind all of that sits the post-EQ spectrum fed by
 *  the native analyser (src/dsp/common/SpectrumAnalyser.h) at ~30 Hz.
 *
 *  Two axes share the canvas: the response curve reads ±18 dB of *gain*, the
 *  analyser reads 0..-72 dBFS of *level*. They are deliberately not the same
 *  scale — the RTA is context for the curve, not a second copy of it — so the
 *  analyser is drawn dim and unlabelled while the curve keeps the axis marks.
 *
 *  The grid and the band curves are cached in an offscreen layer and only
 *  rebuilt when a band value changes; an analyser frame just blits that layer
 *  over a freshly drawn spectrum. Painting is coalesced onto the shared
 *  animator, so a burst of parameter messages still costs one paint and the
 *  frame-budget bench measures this widget along with everything else.
 */

import { addTick } from "../animator";
import {
  SPECTRUM_BANDS,
  SPECTRUM_FLOOR_DB,
  spectrumBandHz,
} from "../bridge/protocol";

export interface EqBandValues {
  type: number; // 0 bell, 1 low shelf, 2 high shelf, 3 HP, 4 LP
  freq: number; // Hz
  gain: number; // dB
  q: number;
  on: boolean;
  solo: boolean;
}

interface Coeffs {
  b0: number; b1: number; b2: number; a1: number; a2: number;
}

const MIN_HZ = 20;
const MAX_HZ = 22000;
const MIN_DB = -18;
const MAX_DB = 18;

// Analyser level axis, in dBFS.
const RTA_TOP_DB = 0;
const RTA_BOTTOM_DB = -72;

const BAND_COLORS = ["#ff6b6b", "#ffa94d", "#ffd43b", "#69db7c", "#4dabf7", "#b197fc"];

export class EqCurve {
  readonly el: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private layer: HTMLCanvasElement;
  private layerCtx: CanvasRenderingContext2D;
  private layerDirty = true;
  private dirty = true;
  private removeTick: () => void;
  private w = 0;
  private h = 0;
  private bands: EqBandValues[] = [];
  private spectrum: Float32Array | null = null;

  /** onBandDrag(bandIndex, freqHz, gainDb) while dragging a handle. */
  constructor(
    width: number,
    height: number,
    private onBandDrag?: (band: number, freq: number, gain: number) => void,
    private onBandGesture?: (band: number, active: boolean) => void,
  ) {
    this.el = document.createElement("canvas");
    this.el.className = "eq-curve";
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    this.w = width;
    this.h = height;
    this.el.width = width * dpr;
    this.el.height = height * dpr;
    this.el.style.width = `${width}px`;
    this.el.style.height = `${height}px`;
    this.ctx = this.el.getContext("2d")!;
    this.ctx.scale(dpr, dpr);

    this.layer = document.createElement("canvas");
    this.layer.width = this.el.width;
    this.layer.height = this.el.height;
    this.layerCtx = this.layer.getContext("2d")!;
    this.layerCtx.scale(dpr, dpr);

    this.removeTick = addTick(() => {
      if (!this.dirty) return;
      this.dirty = false;
      this.draw();
    });

    if (onBandDrag) this.bindDrag();
  }

  setBands(bands: EqBandValues[]): void {
    this.bands = bands;
    this.layerDirty = true;
    this.schedule();
  }

  /** Analyser bands in dBFS. The caller reuses its buffer, so this copies. */
  setSpectrum(db: Float32Array): void {
    if (this.spectrum === null) this.spectrum = new Float32Array(SPECTRUM_BANDS);
    this.spectrum.set(db);
    this.schedule();
  }

  dispose(): void {
    this.removeTick();
  }

  private schedule(): void {
    this.dirty = true;
  }

  // --- geometry ------------------------------------------------------------------

  private xForHz(hz: number): number {
    const t = Math.log(hz / MIN_HZ) / Math.log(MAX_HZ / MIN_HZ);
    return t * this.w;
  }
  private hzForX(x: number): number {
    return MIN_HZ * Math.pow(MAX_HZ / MIN_HZ, x / this.w);
  }
  private yForDb(db: number): number {
    return this.h * (1 - (db - MIN_DB) / (MAX_DB - MIN_DB));
  }
  private yForLevelDb(db: number): number {
    const t = (db - RTA_BOTTOM_DB) / (RTA_TOP_DB - RTA_BOTTOM_DB);
    return this.h * (1 - Math.max(0, Math.min(1, t)));
  }
  private dbForY(y: number): number {
    return MIN_DB + (1 - y / this.h) * (MAX_DB - MIN_DB);
  }

  // --- RBJ coefficients (mirror of Biquad.h) --------------------------------------

  private coeffs(b: EqBandValues, fs: number): Coeffs {
    const w0 = (2 * Math.PI * Math.min(b.freq, 0.49 * fs)) / fs;
    const cw = Math.cos(w0);
    const sw = Math.sin(w0);
    const alpha = sw / (2 * b.q);
    const A = Math.pow(10, b.gain / 40);

    let b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (b.type) {
      case 0: // peaking
        b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
        break;
      case 1: { // low shelf
        const s = 2 * Math.sqrt(A) * alpha;
        b0 = A * (A + 1 - (A - 1) * cw + s);
        b1 = 2 * A * (A - 1 - (A + 1) * cw);
        b2 = A * (A + 1 - (A - 1) * cw - s);
        a0 = A + 1 + (A - 1) * cw + s;
        a1 = -2 * (A - 1 + (A + 1) * cw);
        a2 = A + 1 + (A - 1) * cw - s;
        break;
      }
      case 2: { // high shelf
        const s = 2 * Math.sqrt(A) * alpha;
        b0 = A * (A + 1 + (A - 1) * cw + s);
        b1 = -2 * A * (A - 1 + (A + 1) * cw);
        b2 = A * (A + 1 + (A - 1) * cw - s);
        a0 = A + 1 - (A - 1) * cw + s;
        a1 = 2 * (A - 1 - (A + 1) * cw);
        a2 = A + 1 - (A - 1) * cw - s;
        break;
      }
      case 3: // highpass
        b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        break;
      case 4: // lowpass
        b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        break;
    }
    return { b0: b0 / a0, b1: b1 / a0, b2: b2 / a0, a1: a1 / a0, a2: a2 / a0 };
  }

  private magDb(c: Coeffs, w: number): number {
    // |H(e^jw)| for a biquad.
    const cosw = Math.cos(w), sinw = Math.sin(w);
    const cos2 = Math.cos(2 * w), sin2 = Math.sin(2 * w);
    const numRe = c.b0 + c.b1 * cosw + c.b2 * cos2;
    const numIm = -(c.b1 * sinw + c.b2 * sin2);
    const denRe = 1 + c.a1 * cosw + c.a2 * cos2;
    const denIm = -(c.a1 * sinw + c.a2 * sin2);
    const num = Math.hypot(numRe, numIm);
    const den = Math.hypot(denRe, denIm) || 1e-9;
    return 20 * Math.log10(num / den);
  }

  // --- drawing --------------------------------------------------------------------

  /** One composited frame: background, analyser, then the cached curve layer. */
  private draw(): void {
    const { ctx, w, h } = this;

    const bg = ctx.createLinearGradient(0, 0, 0, h);
    bg.addColorStop(0, "#0d1015");
    bg.addColorStop(1, "#070809");
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, w, h);

    this.drawSpectrum();

    if (this.layerDirty) {
      this.drawLayer();
      this.layerDirty = false;
    }
    ctx.drawImage(this.layer, 0, 0, w, h);
  }

  /** Analyser bars, drawn as one filled area so 96 bands cost one path. */
  private drawSpectrum(): void {
    const spec = this.spectrum;
    if (spec === null) return;

    const { ctx, w, h } = this;

    ctx.beginPath();
    ctx.moveTo(0, h);
    for (let b = 0; b < SPECTRUM_BANDS; b++) {
      const db = spec[b];
      const x = this.xForHz(spectrumBandHz(b));
      // The floor sits below the visible axis, so silence rests on the baseline
      // instead of drawing a hard line across the bottom of the display.
      ctx.lineTo(x, this.yForLevelDb(db <= SPECTRUM_FLOOR_DB + 1 ? RTA_BOTTOM_DB : db));
    }
    ctx.lineTo(w, h);
    ctx.closePath();

    const fill = ctx.createLinearGradient(0, 0, 0, h);
    fill.addColorStop(0, "rgba(130,185,240,0.20)");
    fill.addColorStop(0.55, "rgba(85,140,205,0.10)");
    fill.addColorStop(1, "rgba(55,100,165,0.03)");
    ctx.fillStyle = fill;
    ctx.fill();

    ctx.strokeStyle = "rgba(150,200,245,0.32)";
    ctx.lineWidth = 1;
    ctx.stroke();
  }

  /** Grid, band curves and handles — rebuilt only when a band value changes. */
  private drawLayer(): void {
    const { w, h } = this;
    const ctx = this.layerCtx;
    const fs = 48000;
    ctx.clearRect(0, 0, w, h);

    // dB grid.
    ctx.strokeStyle = "rgba(120,140,160,0.12)";
    ctx.fillStyle = "rgba(150,170,190,0.5)";
    ctx.font = "9px 'SF Mono', Consolas, monospace";
    ctx.lineWidth = 1;
    for (const db of [-12, -6, 0, 6, 12]) {
      const y = this.yForDb(db);
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.strokeStyle = db === 0 ? "rgba(120,140,160,0.28)" : "rgba(120,140,160,0.1)";
      ctx.stroke();
      ctx.fillText(`${db > 0 ? "+" : ""}${db}`, 3, y - 2);
    }
    // Frequency grid.
    for (const hz of [50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
      const x = this.xForHz(hz);
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
      ctx.strokeStyle = "rgba(120,140,160,0.08)";
      ctx.stroke();
      ctx.fillText(hz >= 1000 ? `${hz / 1000}k` : `${hz}`, x + 2, h - 3);
    }

    const active = this.bands.map(
      (b) => b.on && (!this.bands.some((x) => x.solo) || b.solo),
    );

    const N = 220;
    const coeffCache = this.bands.map((b) => this.coeffs(b, fs));

    // Per-band ghost curves.
    for (let bi = 0; bi < this.bands.length; bi++) {
      if (!active[bi]) continue;
      ctx.beginPath();
      for (let i = 0; i <= N; i++) {
        const x = (i / N) * w;
        const hz = this.hzForX(x);
        const wRad = (2 * Math.PI * hz) / fs;
        const y = this.yForDb(this.magDb(coeffCache[bi], wRad));
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      }
      ctx.strokeStyle = BAND_COLORS[bi % BAND_COLORS.length] + "66";
      ctx.lineWidth = 1;
      ctx.stroke();
    }

    // Summed response.
    ctx.beginPath();
    for (let i = 0; i <= N; i++) {
      const x = (i / N) * w;
      const hz = this.hzForX(x);
      const wRad = (2 * Math.PI * hz) / fs;
      let sum = 0;
      for (let bi = 0; bi < this.bands.length; bi++)
        if (active[bi]) sum += this.magDb(coeffCache[bi], wRad);
      const y = this.yForDb(Math.max(MIN_DB, Math.min(MAX_DB, sum)));
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.strokeStyle = "#cfe4ff";
    ctx.lineWidth = 2;
    ctx.shadowColor = "rgba(120,180,255,0.6)";
    ctx.shadowBlur = 6;
    ctx.stroke();
    ctx.shadowBlur = 0;

    // Fill under the curve.
    ctx.lineTo(w, this.yForDb(0));
    ctx.lineTo(0, this.yForDb(0));
    ctx.closePath();
    ctx.fillStyle = "rgba(120,180,255,0.06)";
    ctx.fill();

    // Band handles.
    for (let bi = 0; bi < this.bands.length; bi++) {
      if (!active[bi]) continue;
      const b = this.bands[bi];
      const showsGain = b.type <= 2;
      const x = this.xForHz(b.freq);
      const y = this.yForDb(showsGain ? b.gain : 0);
      ctx.beginPath();
      ctx.arc(x, y, 6, 0, 2 * Math.PI);
      ctx.fillStyle = BAND_COLORS[bi % BAND_COLORS.length];
      ctx.fill();
      ctx.strokeStyle = "rgba(0,0,0,0.6)";
      ctx.lineWidth = 1;
      ctx.stroke();
      ctx.fillStyle = "#0b0c0e";
      ctx.font = "bold 8px sans-serif";
      ctx.fillText(`${bi + 1}`, x - 2.5, y + 3);
    }
  }

  // --- interaction ------------------------------------------------------------------

  private bindDrag(): void {
    let dragBand = -1;

    const handleAt = (px: number, py: number): number => {
      for (let bi = 0; bi < this.bands.length; bi++) {
        const b = this.bands[bi];
        const x = this.xForHz(b.freq);
        const y = this.yForDb(b.type <= 2 ? b.gain : 0);
        if (Math.hypot(px - x, py - y) < 12) return bi;
      }
      return -1;
    };

    this.el.addEventListener("pointerdown", (e) => {
      const r = this.el.getBoundingClientRect();
      dragBand = handleAt(e.clientX - r.left, e.clientY - r.top);
      if (dragBand >= 0) {
        this.el.setPointerCapture(e.pointerId);
        this.onBandGesture?.(dragBand, true);
      }
    });

    this.el.addEventListener("pointermove", (e) => {
      if (dragBand < 0) return;
      const r = this.el.getBoundingClientRect();
      const hz = Math.max(MIN_HZ, Math.min(MAX_HZ, this.hzForX(e.clientX - r.left)));
      const db = Math.max(MIN_DB, Math.min(MAX_DB, this.dbForY(e.clientY - r.top)));
      this.onBandDrag?.(dragBand, hz, db);
    });

    const end = (e: PointerEvent) => {
      if (dragBand < 0) return;
      this.onBandGesture?.(dragBand, false);
      this.el.releasePointerCapture(e.pointerId);
      dragBand = -1;
    };
    this.el.addEventListener("pointerup", end);
    this.el.addEventListener("pointercancel", end);
  }
}
