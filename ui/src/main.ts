import "./styles.css";

import { createBridge } from "./bridge/juce";
import { Store } from "./store";
import { RackFrame } from "./rack/RackFrame";
import { BrowserPanel } from "./rack/BrowserPanel";
import { Toolbar } from "./rack/Toolbar";
import { DragManager } from "./rack/dnd";
import { pumpTicks } from "./animator";

const app = document.getElementById("app")!;
app.className = "studio";

const bridge = createBridge();
const store = new Store(bridge);
const drag = new DragManager(store);

// Layout: [ module browser | scrollable stage (toolbar + rack) ].
const workspace = document.createElement("div");
workspace.className = "workspace";

const browser = new BrowserPanel(store, drag);

const stageWrap = document.createElement("div");
stageWrap.className = "stage-wrap";

const stage = document.createElement("div");
stage.className = "stage";

const toolbar = new Toolbar(store);
const rack = new RackFrame(store, drag);
stage.append(toolbar.el, rack.el);
stageWrap.appendChild(stage);

workspace.append(browser.el, stageWrap);
app.appendChild(workspace);

if (bridge.isMock) {
  const badge = document.createElement("div");
  badge.className = "dev-badge";
  badge.textContent = "browser preview - no audio engine";
  app.appendChild(badge);
}

// The stage (toolbar + rack) is designed at fixed logical pixels and scaled to
// fit its column, so faceplate geometry stays proportionally correct at every
// window size. The browser panel sits outside the scaled stage.
const DESIGN_WIDTH = 1060;

function rescale(): void {
  const scale = stageWrap.clientWidth / DESIGN_WIDTH;
  stage.style.transform = `scale(${scale})`;
}

new ResizeObserver(rescale).observe(stageWrap);
rescale();

// Tell the backend we're alive; it answers with rack layout + all params.
store.start();

// QA hook ("?miditest"): drive the real right-click -> "MIDI Learn" path on the
// first mappable control so the armed ring and, once the mock binds a synthetic
// CC, the "CCn" badge can be screenshotted headlessly. Dev-only, like ?bench.
if (window.location.search.includes("miditest")) {
  window.setTimeout(() => {
    const target = document.querySelector<HTMLElement>(".midi-target");
    if (!target) return;
    const r = target.getBoundingClientRect();
    target.dispatchEvent(
      new MouseEvent("contextmenu", { bubbles: true, clientX: r.left + 4, clientY: r.top + 4 }),
    );
    document.querySelector<HTMLElement>(".context-item")?.click();
  }, 600);
}

// Frame-budget benchmark ("?full&bench"): simulates 600 frames of a fully
// populated rack — meter ingestion, every animation tick, plus a forced
// style/layout flush — and reports per-frame main-thread cost. Compositing
// is excluded (meters are transform/class-only, handled off-main-thread).
if (window.location.search.includes("bench")) {
  window.setTimeout(() => {
    const pump = (window as unknown as Record<string, unknown>).__arPump as
      | (() => void)
      | undefined;

    const times: number[] = [];
    for (let i = 0; i < 600; i++) {
      const t0 = performance.now();
      pump?.();
      pumpTicks(1 / 60);
      void document.body.offsetWidth; // force style/layout
      times.push(performance.now() - t0);
    }

    times.sort((a, b) => a - b);
    const q = (p: number) => times[Math.min(times.length - 1, Math.floor(p * times.length))];
    console.log(
      `ARBENCH frames=${times.length} p50=${q(0.5).toFixed(3)}ms ` +
        `p95=${q(0.95).toFixed(3)}ms p99=${q(0.99).toFixed(3)}ms ` +
        `max=${times[times.length - 1].toFixed(3)}ms`,
    );
  }, 1500);
}
