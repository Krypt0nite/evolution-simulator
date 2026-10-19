// Runs batch experiments off the main thread with its own engine instance.
// One simulation per evo_exp_next() call, so progress is reported per run and
// a cancel request takes effect between runs.
import { Engine, loadModule } from "./engine";

type Msg =
  | { type: "run"; baseUrl: string; spec: unknown }
  | { type: "cancel" };

let cancelled = false;
let engine: Engine | null = null;

self.onmessage = async (e: MessageEvent<Msg>) => {
  const msg = e.data;
  if (msg.type === "cancel") {
    cancelled = true;
    return;
  }
  cancelled = false;
  try {
    engine ??= new Engine(await loadModule(msg.baseUrl));
    const total = engine.expBegin(JSON.stringify(msg.spec));
    const started = performance.now();
    for (let done = 0; done < total; ) {
      if (cancelled) {
        postMessage({ type: "cancelled", summary: engine.expSummary() });
        return;
      }
      done = engine.expNext();
      if (done < 0) throw new Error(engine.lastError());
      postMessage({ type: "progress", done, total, elapsed: (performance.now() - started) / 1000 });
    }
    postMessage({ type: "done", summary: engine.expSummary(), elapsed: (performance.now() - started) / 1000 });
  } catch (err) {
    postMessage({ type: "error", message: err instanceof Error ? err.message : String(err) });
  }
};
