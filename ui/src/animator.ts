/** One shared requestAnimationFrame loop for every animated widget (meter
 *  ballistics, LED decay, needle physics). One loop instead of N keeps the
 *  frame budget predictable; callbacks receive dt in seconds, clamped so a
 *  background tab doesn't catapult springs on resume.
 */

type Tick = (dt: number) => void;

const ticks = new Set<Tick>();
let last = performance.now();
let running = false;

function frame(now: number): void {
  const dt = Math.min(0.1, (now - last) / 1000);
  last = now;
  ticks.forEach((fn) => fn(dt));
  if (ticks.size > 0) {
    requestAnimationFrame(frame);
  } else {
    running = false;
  }
}

/** Benchmark hook: run every registered tick once, synchronously. */
export function pumpTicks(dt: number): void {
  ticks.forEach((fn) => fn(dt));
}

export function addTick(fn: Tick): () => void {
  ticks.add(fn);
  if (!running) {
    running = true;
    last = performance.now();
    requestAnimationFrame(frame);
  }
  return () => ticks.delete(fn);
}
