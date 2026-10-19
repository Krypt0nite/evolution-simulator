#!/usr/bin/env node
// Runs the same config + seed through the native CLI and the WebAssembly build
// and compares full-state hashes every N ticks. Reports the first divergence.
//
// Usage: node scripts/compare_native_wasm.mjs [--ticks 3000] [--every 100]
//        [--native build/evosim] [--wasm build-wasm/evosim.mjs] [--config FILE] [--set k=v,...]
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { createRequire } from "node:module";

// Emscripten 3.1.x ES6 glue still calls CommonJS require()/__dirname under Node.
globalThis.require = createRequire(import.meta.url);

const args = Object.fromEntries(
  process.argv.slice(2).reduce((acc, a, i, all) => {
    if (a.startsWith("--")) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith("--") ? all[i + 1] : "1"]);
    return acc;
  }, []),
);
const ticks = Number(args.ticks ?? 3000);
const every = Number(args.every ?? 100);
const nativeBin = args.native ?? "build/evosim";
const wasmPath = path.resolve(args.wasm ?? "build-wasm/evosim.mjs");

// Effective config: take it from the native binary so both sides see identical JSON.
const cfgArgs = [];
if (args.config) cfgArgs.push("--config", args.config);
if (args.set) cfgArgs.push("--set", args.set);
const baseCfg = args.config ? JSON.parse(readFileSync(args.config, "utf8")) : JSON.parse(execFileSync(nativeBin, ["default-config"]).toString());
if (args.set) {
  for (const item of args.set.split(",")) {
    const [k, v] = item.split("=");
    let val;
    try { val = JSON.parse(v); } catch { val = v; }
    const keys = k.split(".");
    let o = baseCfg;
    for (const key of keys.slice(0, -1)) o = o[key] ??= {};
    o[keys.at(-1)] = val;
  }
}

const native = execFileSync(nativeBin, ["hash", ...cfgArgs, "--ticks", String(ticks), "--every", String(every)])
  .toString().trim().split(/\r?\n/).filter((l) => l.includes(" "))  // CRLF on Windows
  .map((l) => l.trim().split(" "));

globalThis.__dirname = path.dirname(wasmPath);
const { default: createEvoModule } = await import(pathToFileURL(wasmPath).href);
const M = await createEvoModule({ wasmBinary: readFileSync(wasmPath.replace(/\.mjs$/, ".wasm")) });
const str = (p) => M.UTF8ToString(p);
const withString = (s, fn) => {
  const n = M.lengthBytesUTF8(s) + 1;
  const p = M._malloc(n);
  M.stringToUTF8(s, p, n);
  try { return fn(p); } finally { M._free(p); }
};
if (withString(JSON.stringify(baseCfg), (p) => M._evo_create(p)) !== 1) {
  console.error("evo_create failed:", str(M._evo_last_error()));
  process.exit(2);
}

let firstDivergence = null;
let matched = 0;
for (const [tick, hash] of native) {
  const target = Number(tick);
  while (M._evo_tick() < target) M._evo_step(1);
  const w = str(M._evo_hash());
  if (w === hash) matched++;
  else if (!firstDivergence) firstDivergence = { tick: target, native: hash, wasm: w };
}
const label = `${args.config ?? "default"}${args.set ? " + " + args.set : ""}`;
if (!firstDivergence) {
  console.log(`IDENTICAL  ${label}: native and WASM state hashes match at all ${matched} checkpoints through tick ${ticks}`);
} else {
  console.log(`DIVERGED   ${label}: matched ${matched}/${native.length} checkpoints; first divergence by tick ${firstDivergence.tick} (native ${firstDivergence.native}, wasm ${firstDivergence.wasm})`);
  process.exitCode = 1;
}
