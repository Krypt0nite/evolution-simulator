// Copies the compiled engine and the shared config/experiment presets into
// public/ so the browser loads exactly what the native CLI uses.
import { copyFileSync, existsSync, mkdirSync, readdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "../..");
const pub = path.resolve(here, "../public");
const wasmDir = process.env.EVOSIM_WASM_DIR ?? path.join(root, "build-wasm");

// public/wasm/ is committed so hosts without Emscripten (Vercel) can build the
// site; a fresh local engine build in build-wasm/ overwrites it.
mkdirSync(path.join(pub, "wasm"), { recursive: true });
for (const f of ["evosim.mjs", "evosim.wasm"]) {
  const src = path.join(wasmDir, f);
  const dest = path.join(pub, "wasm", f);
  if (existsSync(src)) {
    copyFileSync(src, dest);
  } else if (!existsSync(dest)) {
    console.error(`Missing ${src}. Build the engine first:\n  emcmake cmake -S . -B build-wasm && cmake --build build-wasm`);
    process.exit(1);
  }
}

const presets = [];
mkdirSync(path.join(pub, "configs", "experiments"), { recursive: true });
for (const f of readdirSync(path.join(root, "config")).filter((f) => f.endsWith(".json")).sort()) {
  copyFileSync(path.join(root, "config", f), path.join(pub, "configs", f));
  presets.push(f.replace(/\.json$/, ""));
}
const experiments = [];
for (const f of readdirSync(path.join(root, "config", "experiments")).filter((f) => f.endsWith(".json")).sort()) {
  copyFileSync(path.join(root, "config", "experiments", f), path.join(pub, "configs", "experiments", f));
  experiments.push(f.replace(/\.json$/, ""));
}
writeFileSync(path.join(pub, "configs", "index.json"), JSON.stringify({ presets, experiments }, null, 2));
console.log(`synced engine + ${presets.length} presets + ${experiments.length} experiments into public/`);
