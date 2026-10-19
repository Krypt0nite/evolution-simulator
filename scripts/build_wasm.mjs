// Builds the WebAssembly engine (build-wasm/evosim.mjs + evosim.wasm) without CMake.
// Uses the same flags as the EMSCRIPTEN branch of CMakeLists.txt, so the output is
// interchangeable with `emcmake cmake -S . -B build-wasm && cmake --build build-wasm`.
//
// Usage: node scripts/build_wasm.mjs
// Looks for the Emscripten SDK in $EMSDK, then in .tools/emsdk (local install).
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const emsdk = process.env.EMSDK ?? path.join(root, ".tools", "emsdk");
const emxx = path.join(emsdk, "upstream", "emscripten", "em++.py");
if (!existsSync(emxx)) {
  console.error(`Emscripten not found at ${emsdk}. Install it (see README, "WebAssembly") or set EMSDK.`);
  process.exit(1);
}

// Prefer the Python that emsdk installed, then the portable one in .tools, then the system one.
const pythons = [
  ...["python/3.13.3_64bit/python.exe"].map((p) => path.join(emsdk, p)),
  path.join(root, ".tools", "python", "python.exe"),
];
const python = pythons.find(existsSync) ?? (process.platform === "win32" ? "python" : "python3");

const version = /project\(\s*\w+\s+VERSION\s+([\d.]+)/.exec(readFileSync(path.join(root, "CMakeLists.txt"), "utf8"))?.[1] ?? "0.0.0";

// Engine library sources, in the order CMakeLists.txt lists them.
const sources = [
  "src/core/Json.cpp", "src/core/SimulationConfig.cpp", "src/core/SpatialGrid.cpp", "src/core/Simulation.cpp",
  "src/organisms/Genome.cpp", "src/organisms/Phenotype.cpp", "src/organisms/Brain.cpp",
  "src/ecology/ResourceField.cpp", "src/ecology/Environment.cpp",
  "src/analytics/LineageTracker.cpp", "src/analytics/Speciation.cpp", "src/analytics/Statistics.cpp",
  "src/analytics/StatsMath.cpp", "src/analytics/ExperimentRunner.cpp",
  "src/serialization/Checkpoint.cpp",
  "src/interfaces/wasm_api.cpp",
];

const outDir = path.join(root, "build-wasm");
mkdirSync(outDir, { recursive: true });

const args = [
  emxx,
  "-std=c++20", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow",
  "-ffp-contract=off", "-fexceptions",
  `-DEVOSIM_VERSION="${version}"`,
  "-Isrc",
  ...sources,
  "-o", path.join(outDir, "evosim.mjs"),
  "-sMODULARIZE=1", "-sEXPORT_ES6=1", "-sEXPORT_NAME=createEvoModule",
  "-sENVIRONMENT=web,worker,node",
  "-sALLOW_MEMORY_GROWTH=1", "-sINITIAL_MEMORY=64MB", "-sTOTAL_STACK=1MB",
  "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU8','HEAPF32','HEAPU32']",
  "-sEXPORTED_FUNCTIONS=['_malloc','_free']",
];

console.log(`building engine ${version} with ${emsdk}`);
execFileSync(python, args, {
  cwd: root,
  stdio: "inherit",
  // EMSDK_PYTHON: emcc.bat (used when Emscripten rebuilds its system libraries) needs it on Windows.
  env: { ...process.env, EMSDK: emsdk, EM_CONFIG: path.join(emsdk, ".emscripten"), EMSDK_PYTHON: python },
});
console.log(`wrote ${path.relative(root, outDir)}/evosim.mjs and evosim.wasm`);
