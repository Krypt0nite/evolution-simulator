# Architecture

EvoLab Evolution Simulator is one C++20 engine with three front doors:

```
                ┌──────────────────────────── evosim_core (static lib, C++20) ───────────────────────────┐
                │ core/        Simulation · SimulationConfig · Json · Random · SpatialGrid · Vec2          │
                │ organisms/   Genome · Phenotype · Organism · Brain (Utility / Neural controllers)       │
                │ ecology/     ResourceField · Environment (temperature, hazards, events) · Reproduction  │
                │ analytics/   Statistics · LineageTracker · Speciation · StatsMath · ExperimentRunner    │
                │ serialization/ Checkpoint (binary, versioned)                                            │
                └───────────────▲──────────────────────────────▲────────────────────────────▲────────────┘
                                │                              │                            │
                 apps/evosim_cli.cpp              src/interfaces/wasm_api.cpp       tests/*.cpp (Catch2)
                 run · experiment · bench          extern "C" exports → Emscripten   unit · integration ·
                 · checkpoint · hash                → web/ (Vite + TypeScript)        property · sanity
```

The engine has no knowledge of rendering. The browser receives flat typed-array snapshots
(`Float32Array` of organism render data, `Uint8Array` of food density) and JSON for
inspection/statistics. TypeScript never decides anything about organisms; it only draws and
sends commands (step, reset, apply environment event, save/load).

## Simulation state

`Simulation` owns everything; there is no global mutable state.

| Member | Purpose |
|---|---|
| `SimulationConfig cfg_` | Validated, versioned parameters (from JSON) |
| `Rng rng_` | The single `std::mt19937_64` stream; seeded from `cfg.seed` |
| `std::vector<Organism> organisms_` | Living organisms, kept sorted by id between ticks |
| `ResourceField food_` | Plant biomass grid with logistic regrowth |
| `Environment env_` | Temperature field, hazards, scheduled and interactive events |
| `SpatialGrid grid_` | Uniform bucket grid rebuilt from positions (twice per tick) |
| `LineageTracker lineage_` | Birth/death record for every organism ever born (dense by id) |
| `Speciation species_` | Periodic genome clustering with stable cluster ids |
| `Statistics stats_` | Time series of sampled metrics |
| `EnergyLedger ledger_` | Cumulative energy in/out by channel |

## Update order (one tick, fixed `dt = 1`)

1. **Environment** – fire scheduled events for this tick; update global temperature/season.
2. **Index** – rebuild `SpatialGrid` from start-of-tick positions.
3. **Sense + decide** – every organism builds a `Perception` from what is inside its vision
   radius and its controller returns an `Intent`. Reads only start-of-tick state, so the
   result is independent of iteration order.
4. **Move** – apply intents, clamp to world bounds, charge movement energy.
5. **Re-index** – rebuild the grid at post-move positions; draw a seeded random permutation
   used for all contested interactions this tick (no id-order bias).
6. **Feed** – prey graze the food cell under them (permutation order → first-come contention).
7. **Predation** – predators whose intent was `Hunt` attempt one capture on a prey in contact.
8. **Metabolism + ageing** – basal, thermal and hazard costs; age++; death by starvation or
   senescence hazard (rolled in id order).
9. **Reproduction** – asexual or sexual (contact pairing) in permutation order; children go to
   a pending buffer, never into `organisms_` mid-loop.
10. **Commit** – remove dead organisms (stable compaction), append births (ids increase, so the
    vector stays id-sorted), record deaths/births in the lineage tracker.
11. **Regrow** – resource logistic regrowth (energy enters the world here).
12. **Bookkeeping** – tick++, clustering every N ticks, statistics sample every M ticks.

Because creation/removal only happens in step 10, no iterator or index is invalidated during
the tick. Dead-this-tick organisms are flagged `alive=false` and skipped by every later phase
(property-tested: dead organisms never eat, move, hunt or reproduce).

## Randomness and determinism

* One `mt19937_64` stream. Its output sequence is fully specified by the C++ standard.
* `std::uniform_*_distribution`, `std::normal_distribution` and `std::shuffle` are
  implementation-defined, so `Rng` implements its own uniform (53-bit), Box–Muller normal and
  Fisher–Yates shuffle. This keeps libstdc++ (native) and libc++ (Emscripten) on identical
  random sequences.
* Statistics and clustering never consume random numbers, so turning analytics on/off or
  changing the sample interval does not change the trajectory.
* Every state-affecting transcendental (`exp`, `log`, `pow`, `sin`, `tanh`) goes
  through `src/core/DetMath.hpp`, implemented only with correctly rounded IEEE-754
  operations, and FMA contraction is disabled (`-ffp-contract=off`). libm versions
  differ in the last bit; in a chaotic system one bit eventually changes everything.
* The state hash fingerprints the RNG by its next outputs, not by its text form
  (libstdc++ and libc++ print the Mersenne Twister state differently).
* Result, measured with `scripts/compare_native_wasm.mjs`: the native build
  (GCC 13/glibc, x86-64) and the WebAssembly build (Clang 15/musl via Emscripten
  3.1.6) produce **identical state hashes at every checkpoint** for the default,
  sexual, neural, predator, climate-event and thermal-niche configurations. Other
  compiler/CPU pairs should match by construction but have not been verified.

## Performance design

* Uniform grid (`SpatialGrid`, counting-sort into a flat index array) gives O(n·k) neighbour
  queries; no all-pairs loops anywhere.
* Organisms are an array of structs (`Organism` plus a genome vector). Profiling
  (`evosim bench`, table in the README) shows sensing and deciding take 71–86% of a
  tick, mostly the food-field scan, while the spatial index costs 2–3%. Memory
  layout is not the bottleneck, so a struct-of-arrays rewrite is not justified.
  The next optimisation, if needed, is a coarse max-biomass pyramid so the food
  scan can skip empty regions.
* Perception caps the number of neighbours *evaluated* (attention limit, configurable), while
  the grid query itself is exact (property-tested against brute force).
* Food perception samples the vision disc on a stride so cost per organism is bounded.
* Rendering pulls a packed `Float32Array` snapshot once per animation frame; simulation speed
  (ticks/frame) is independent of render rate.
* Single-threaded by design until profiling shows a need; phases 3 and 4 are embarrassingly
  parallel and are the obvious targets. Batch experiments are parallel across seeds instead
  (`--jobs`), which keeps every run deterministic.

## Persistence

* Config: JSON, `"version": 1`, validated with explicit error messages; unknown keys rejected.
* Checkpoint: binary, magic `EVOCKPT`, format version, engine version, embedded config JSON,
  RNG state, all organisms, food grid, environment, lineage, species and statistics.
  Resume-from-checkpoint is tested to be bit-identical to an uninterrupted run.
* Experiments: `runs.csv` (one row per seed × condition), `timeseries.csv`,
  `summary.csv` (mean, sd, 95% t-CI, Cohen's d vs. the first condition), `meta.json`.
