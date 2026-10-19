// Detects notable events from the engine's statistics: extinction, population
// crash and recovery, predators appearing or dying out, and new genetic clusters.
//
// Read-only by construction: it only sees Sample objects and cluster summaries
// already copied out of the engine as JSON, never the engine itself, so it cannot
// touch the simulation state or its random-number generator. Population events are
// detected on the engine's own sample ticks (not on UI refreshes), so the same run
// produces the same observations whatever the UI update rate.
import { fmtInt } from "./charts";
import type { Sample, StatsPayload } from "./engine";
import { geneLabel } from "./util";

export type ObservationKind = "extinction" | "crash" | "recovery" | "predators-in" | "predators-out" | "cluster";

export interface Observation {
  tick: number;
  kind: ObservationKind;
  title: string;
  detail: string;
  danger?: boolean;
}

/** A population "crash" is a fall below this share of the peak since the last event. */
const CRASH_FRACTION = 0.5;
/** It has "recovered" once it is back to this share of the pre-crash peak. */
const RECOVERY_FRACTION = 0.9;
/** Ignore crashes from peaks this small; drift dominates tiny populations. */
const MIN_PEAK = 40;
/** A new cluster is reported once it holds this many organisms and this share of its kind. */
const CLUSTER_MIN_SIZE = 10;
const CLUSTER_MIN_SHARE = 0.05;

export class Observer {
  readonly log: Observation[] = [];
  private lastTick = -1;
  private prev: Sample | null = null;
  private peak = 0;
  private crash: { peak: number; low: number; lowTick: number; tick: number } | null = null;
  private seenClusters = new Set<number>();
  private primedKinds = new Set<boolean>();

  reset() {
    this.log.length = 0;
    this.lastTick = -1;
    this.prev = null;
    this.peak = 0;
    this.crash = null;
    this.seenClusters.clear();
    this.primedKinds.clear();
  }

  /** Processes samples not yet seen (by tick) and the current cluster list; returns new observations. */
  update(samples: Sample[], payload: StatsPayload): Observation[] {
    const out: Observation[] = [];
    for (const s of samples) {
      if (s.tick <= this.lastTick) continue;
      this.lastTick = s.tick;
      this.population(s, out);
      this.prev = s;
    }
    // The run stops at extinction, possibly between samples; that final tick is fixed, so use it.
    const cur = payload.current;
    if (cur.population === 0 && cur.tick > this.lastTick) {
      this.lastTick = cur.tick;
      this.population(cur, out);
      this.prev = cur;
    }
    this.clusters(payload, out);
    this.log.push(...out);
    if (this.log.length > 60) this.log.splice(0, this.log.length - 60);
    return out;
  }

  private population(s: Sample, out: Observation[]) {
    const p = this.prev;
    if (p && p.population > 0 && s.population === 0) {
      out.push({ tick: s.tick, kind: "extinction", danger: true, title: "Population extinct",
        detail: `No organisms were alive at tick ${fmtInt(s.tick)} (${fmtInt(p.population)} at the previous sample).` });
      this.crash = null;
      return;
    }
    if (p && p.predators === 0 && s.predators > 0) {
      out.push({ tick: s.tick, kind: "predators-in", title: "Predators introduced",
        detail: `${fmtInt(s.predators)} predators present at tick ${fmtInt(s.tick)}, alongside ${fmtInt(s.prey)} prey.` });
    }
    if (p && p.predators > 0 && s.predators === 0 && s.prey > 0) {
      out.push({ tick: s.tick, kind: "predators-out", title: "Predators died out",
        detail: `The last predators were gone by tick ${fmtInt(s.tick)}; ${fmtInt(s.prey)} prey remain.` });
    }
    const pop = s.population;
    if (!this.crash) {
      this.peak = Math.max(this.peak, pop);
      if (this.peak >= MIN_PEAK && pop > 0 && pop < this.peak * CRASH_FRACTION) {
        this.crash = { peak: this.peak, low: pop, lowTick: s.tick, tick: s.tick };
        out.push({ tick: s.tick, kind: "crash", title: "Population halved",
          detail: `Population fell to ${fmtInt(pop)}, below half of its peak of ${fmtInt(this.peak)}.` });
      }
    } else {
      if (pop < this.crash.low) { this.crash.low = pop; this.crash.lowTick = s.tick; }
      if (pop >= this.crash.peak * RECOVERY_FRACTION) {
        out.push({ tick: s.tick, kind: "recovery", title: "Population recovered",
          detail: `Back to ${fmtInt(pop)} organisms, ${fmtInt(s.tick - this.crash.tick)} ticks after it halved ` +
            `(low of ${fmtInt(this.crash.low)} at tick ${fmtInt(this.crash.lowTick)}).` });
        this.crash = null;
        this.peak = pop;
      }
    }
  }

  private clusters(payload: StatsPayload, out: Observation[]) {
    const tick = payload.current.tick;
    const isPred = (c: { prey: number; predators: number }) => c.predators > c.prey;
    // The first clusters of each kind (founders at tick 0, or predators when they are
    // introduced) are where that kind starts, not divergence, so they are not reported.
    for (const pred of [false, true]) {
      if (this.primedKinds.has(pred)) continue;
      const mine = payload.clusters.filter((c) => isPred(c) === pred);
      if (!mine.length) continue;
      for (const c of mine) this.seenClusters.add(c.id);
      this.primedKinds.add(pred);
    }
    const prey = payload.current.prey, preds = payload.current.predators;
    for (const c of payload.clusters) {
      if (this.seenClusters.has(c.id)) continue;
      const kindTotal = isPred(c) ? preds : prey;
      if (c.size < CLUSTER_MIN_SIZE || c.size < kindTotal * CLUSTER_MIN_SHARE) continue;
      this.seenClusters.add(c.id);
      // Compare with the largest other cluster of the same kind (clusters arrive sorted by size).
      const largest = payload.clusters.find((o) => o.id !== c.id && isPred(o) === isPred(c));
      let diff = "";
      if (largest) {
        const keys = Object.keys(c.centroid).filter((g) => !g.startsWith("w"));
        const top = keys
          .map((g) => ({ g, d: c.centroid[g] - largest.centroid[g], rel: Math.abs(c.centroid[g] - largest.centroid[g]) / (Math.abs(largest.centroid[g]) + 1e-9) }))
          .sort((a, b) => b.rel - a.rel).slice(0, 2);
        diff = ` Compared with the largest cluster (#${largest.id}): ` +
          top.map((t) => `${geneLabel(t.g).toLowerCase()} ${t.d > 0 ? "higher" : "lower"}`).join(", ") + ".";
      }
      out.push({ tick, kind: "cluster", title: `New genetic cluster #${c.id}`,
        detail: `${fmtInt(c.size)} ${isPred(c) ? "predators" : "prey"} with genomes distinct from existing clusters.${diff}` });
    }
  }
}
