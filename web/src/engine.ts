// Typed wrapper around the C++ engine compiled to WebAssembly.
// The engine is the single source of truth: this file only marshals commands
// in and snapshots out (see src/interfaces/wasm_api.cpp for the C ABI).

export interface EvoModule {
  HEAPU8: Uint8Array;
  HEAPF32: Float32Array;
  HEAPU32: Uint32Array;
  UTF8ToString(ptr: number): string;
  stringToUTF8(s: string, ptr: number, max: number): void;
  lengthBytesUTF8(s: string): number;
  _malloc(n: number): number;
  _free(p: number): void;
  [fn: `_evo_${string}`]: (...args: number[]) => number;
}

export const RENDER = {
  x: 0, y: 1, hx: 2, hy: 3, size: 4, energy: 5, kind: 6, cluster: 7, vision: 8, action: 9, generation: 10,
} as const;

export const ACTIONS = ["rest", "forage", "explore", "flee", "hunt", "seek-mate", "social", "avoid-hazard", "steer"];

export interface WorldInfo {
  width: number; height: number;
  foodCols: number; foodRows: number; foodCell: number;
  foodMultiplier: number; regrowthRate: number;
  temperatureOffset: number; temperatureCycle: number; temperatureGradient: number;
  hazards: { x: number; y: number; radius: number; damage: number }[];
  seed: number; tick: number; controller: string; reproduction: string; predatorsEnabled: boolean;
  engineVersion: string; lineageTruncated: boolean;
}

export type TraitMap = Record<string, number>;

export interface Sample {
  tick: number; population: number; prey: number; predators: number;
  births: number; deaths: number; deathsStarvation: number; deathsOldAge: number; deathsPredation: number;
  birthsSuppressed: number; meanEnergyFrac: number; meanAge: number; meanAgeAtDeath: number;
  medianAgeAtDeath: number; offspringPerDeath: number; meanGeneration: number; maxGeneration: number;
  diversity: number; foodTotal: number; foodFraction: number; temperatureOffset: number;
  clusters: number; lineages: number; stepMs: number;
  traitMean: TraitMap; traitSd: TraitMap; predTraitMean: TraitMap;
}

export interface StatsPayload {
  total: number;
  samples: Sample[];
  current: Sample;
  totalBirths: number;
  totalDeaths: number;
  ledger: Record<string, number>;
  clusters: { id: number; size: number; prey: number; predators: number; centroid: TraitMap }[];
  lineages: { founder: number; alive: number }[];
}

export interface GeneSpec { name: string; min: number; max: number; initMin: number; initMax: number; description: string }

export class EngineError extends Error {}

export async function loadModule(baseUrl: string): Promise<EvoModule> {
  const url = new URL("wasm/evosim.mjs", baseUrl).href;
  const mod = await import(/* @vite-ignore */ url);
  return (await mod.default({
    locateFile: (p: string) => new URL(`wasm/${p}`, baseUrl).href,
  })) as EvoModule;
}

export class Engine {
  constructor(readonly M: EvoModule) {}

  private str(ptr: number): string {
    return this.M.UTF8ToString(ptr);
  }

  private withString<T>(s: string, fn: (ptr: number) => T): T {
    const n = this.M.lengthBytesUTF8(s) + 1;
    const p = this.M._malloc(n);
    this.M.stringToUTF8(s, p, n);
    try {
      return fn(p);
    } finally {
      this.M._free(p);
    }
  }

  private check(code: number): number {
    if (code < 0 || code === 0) throw new EngineError(this.str(this.M._evo_last_error()) || "engine call failed");
    return code;
  }

  version(): string { return this.str(this.M._evo_version()); }
  genes(): GeneSpec[] { return JSON.parse(this.str(this.M._evo_genes())); }
  defaultConfig(): string { return this.str(this.M._evo_default_config()); }

  validate(configJson: string): string | null {
    const ok = this.withString(configJson, (p) => this.M._evo_validate_config(p));
    return ok === 1 ? null : this.str(this.M._evo_last_error());
  }

  create(configJson: string): void {
    this.check(this.withString(configJson, (p) => this.M._evo_create(p)));
  }

  config(): string { return this.str(this.M._evo_config()); }
  step(n: number): number { return this.M._evo_step(n); }
  tick(): number { return this.M._evo_tick(); }
  count(): number { return this.M._evo_count(); }
  hash(): string { return this.str(this.M._evo_hash()); }
  world(): WorldInfo { return JSON.parse(this.str(this.M._evo_world())); }
  stats(from: number): StatsPayload { return JSON.parse(this.str(this.M._evo_stats(from))); }
  pick(x: number, y: number, r: number): number { return this.M._evo_pick(x, y, r); }
  inspect(id: number): any { return JSON.parse(this.str(this.M._evo_inspect(id))); }

  /** Packed organism data; views are only valid until the next engine call. */
  render(): { data: Float32Array; ids: Uint32Array; count: number; stride: number } {
    const stride = this.M._evo_render_stride();
    const count = this.M._evo_count();
    const ptr = this.M._evo_render_buffer();
    const idPtr = this.M._evo_render_ids();
    return {
      data: new Float32Array(this.M.HEAPF32.buffer, ptr, count * stride),
      ids: new Uint32Array(this.M.HEAPU32.buffer, idPtr, count * 2),
      count,
      stride,
    };
  }

  food(cells: number): Uint8Array {
    const ptr = this.M._evo_food_buffer();
    return new Uint8Array(this.M.HEAPU8.buffer, ptr, cells);
  }

  scheduleEvent(ev: Record<string, unknown>): void {
    this.check(this.withString(JSON.stringify(ev), (p) => this.M._evo_schedule_event(p)));
  }

  saveCheckpoint(): Uint8Array {
    const n = this.check(this.M._evo_checkpoint_save());
    return new Uint8Array(this.M.HEAPU8.buffer, this.M._evo_checkpoint_ptr(), n).slice();
  }

  loadCheckpoint(bytes: Uint8Array): void {
    const p = this.M._malloc(bytes.length);
    this.M.HEAPU8.set(bytes, p);
    try {
      this.check(this.M._evo_checkpoint_load(p, bytes.length));
    } finally {
      this.M._free(p);
    }
  }

  // Batch experiments (used inside the worker).
  expBegin(spec: string): number { return this.check(this.withString(spec, (p) => this.M._evo_exp_begin(p))); }
  expNext(): number { return this.M._evo_exp_next(); }
  expSummary(): any { return JSON.parse(this.str(this.M._evo_exp_summary())); }
  lastError(): string { return this.str(this.M._evo_last_error()); }
}

export function traitValues(engine: Engine, gene: number): Float32Array {
  const n = engine.count();
  const ptr = engine.M._evo_trait_values(gene);
  return new Float32Array(engine.M.HEAPF32.buffer, ptr, n).slice();
}
