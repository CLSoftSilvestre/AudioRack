import "./styles.css";

import { createBridge } from "./bridge/juce";
import { Store } from "./store";
import { RackFrame } from "./rack/RackFrame";

const app = document.getElementById("app")!;
app.className = "studio";

const bridge = createBridge();
const store = new Store(bridge);

const stage = document.createElement("div");
stage.className = "stage";

const rack = new RackFrame(store);
stage.appendChild(rack.el);
app.appendChild(stage);

if (bridge.isMock) {
  const badge = document.createElement("div");
  badge.className = "dev-badge";
  badge.textContent = "browser preview - no audio engine";
  app.appendChild(badge);
}

// The stage is designed at fixed logical pixels and scaled to fit the editor,
// so faceplate geometry stays proportionally correct at every window size.
const DESIGN_WIDTH = 1060;

function rescale(): void {
  const scale = app.clientWidth / DESIGN_WIDTH;
  stage.style.transform = `scale(${scale})`;
}

new ResizeObserver(rescale).observe(app);
rescale();

// Tell the backend we're alive; it answers with rack layout + all params.
store.start();

// Frame-budget benchmark ("?full&bench"): simulates 600 frames of a fully
// populated rack — meter ingestion, every animation tick, plus a forced
// style/layout flush — and reports per-frame main-thread cost. Compositing
// is excluded (meters are transform/class-only, handled off-main-thread).
if (window.location.search.includes("bench")) {
  window.setTimeout(async () => {
    const { pumpTicks } = await import("./animator");
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
