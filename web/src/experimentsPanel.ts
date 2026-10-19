// Experiment dashboard: configure a batch, run it in the worker, compare conditions.
import { Chart, fmt } from "./charts";
import { $, $input, $select, download, escapeHtml, geneLabel } from "./util";

const COND_COLORS = ["#4cc9e0", "#f0b54a", "#6bd39e", "#a191ff", "#ee6e6b", "#ff8cc6"];

const SERIES_LABELS: Record<string, string> = {
  population: "Population",
  prey: "Prey",
  predators: "Predators",
  diversity: "Genetic diversity",
  food_fraction: "Food available (fraction of capacity)",
  mean_generation: "Mean generation",
};

const METRIC_LABELS: Record<string, string> = {
  extinct: "Extinct (share of runs)",
  final_population: "Final population",
  mean_population: "Mean population, second half",
  min_population: "Minimum population",
  final_predators: "Final predators",
  predators_extinct: "Predators extinct (share)",
  mean_lifespan: "Mean lifespan (ticks)",
  median_lifespan: "Median lifespan (ticks)",
  reproductive_success: "Share that reproduced",
  offspring_per_organism: "Offspring per organism",
  total_births: "Total births",
  max_generation: "Max generation",
  final_diversity: "Final diversity",
  mean_diversity: "Mean diversity",
  mean_food_fraction: "Mean food available",
  energy_capture: "Primary production captured",
  recovery_ticks: "Ticks to recover after perturbation",
  final_clusters: "Genetic clusters at end",
  ms_per_tick: "Compute cost (ms per tick)",
};

function seriesLabel(col: string) {
  if (col.startsWith("mean_") && !(col in SERIES_LABELS)) return `${geneLabel(col.slice(5))} (mean)`;
  return SERIES_LABELS[col] ?? col;
}
function metricLabel(m: string) {
  if (m.startsWith("final_") && !(m in METRIC_LABELS)) return `Final ${geneLabel(m.slice(6)).toLowerCase()}`;
  return METRIC_LABELS[m] ?? m;
}

export class ExperimentsPanel {
  private worker: Worker | null = null;
  private specs = new Map<string, any>();
  private summary: any = null;
  private chart: Chart | null = null;
  private outcome = "mean_population";

  constructor(private baseUrl: string, private experimentNames: string[], private baseConfigs: Map<string, any>) {
    const sel = $select("exp-select");
    sel.innerHTML = experimentNames.map((n) => `<option value="${n}">${escapeHtml(n.replace(/^e(\d)_/, "E$1 ").replace(/_/g, " "))}</option>`).join("");
    sel.addEventListener("change", () => this.showSpec());
    $("exp-run").addEventListener("click", () => this.run());
    $("exp-cancel").addEventListener("click", () => this.worker?.postMessage({ type: "cancel" }));
    $select("exp-metric").addEventListener("change", () => this.drawChart());
    $("exp-dl-runs").addEventListener("click", () => this.downloadRuns());
    $("exp-dl-summary").addEventListener("click", () => {
      if (this.summary) download(`${this.summary.experiment}-summary.json`, JSON.stringify(this.summary, null, 1), "application/json");
    });
  }

  async init() {
    await Promise.all(this.experimentNames.map(async (n) => {
      const res = await fetch(new URL(`configs/experiments/${n}.json`, this.baseUrl));
      this.specs.set(n, await res.json());
    }));
    this.showSpec();
  }

  private current() { return this.specs.get($select("exp-select").value); }

  private showSpec() {
    const spec = this.current();
    if (!spec) return;
    $input("exp-seeds").value = String(spec.seeds ?? 8);
    $input("exp-ticks").value = String(spec.ticks ?? 5000);
    const conds = spec.conditions.map((c: any) => {
      const o = Object.entries(c.overrides ?? {}).map(([k, v]) => `${k} = ${JSON.stringify(v)}`).join("; ");
      return `<li><b>${escapeHtml(c.name)}</b>${o ? `: ${escapeHtml(o)}` : " (base configuration)"}</li>`;
    }).join("");
    $("exp-about").innerHTML = `<p>${escapeHtml(spec.description ?? "")}</p>
      <p><b>Question.</b> ${escapeHtml(spec.hypothesis ?? "")}</p><ul>${conds}</ul>
      <p>A full run of ${spec.seeds} seeds × ${spec.conditions.length} conditions × ${spec.ticks} ticks takes a few minutes in the browser; reduce seeds or ticks for a quick look. The native CLI runs the same spec in parallel.</p>`;
  }

  private resolveSpec(): any {
    const spec = structuredClone(this.current());
    if (spec.baseConfig && !spec.base) {
      const name = String(spec.baseConfig).replace(/^.*\//, "").replace(/\.json$/, "");
      spec.base = structuredClone(this.baseConfigs.get(name));
      delete spec.baseConfig;
    }
    spec.seeds = Math.max(2, Number($input("exp-seeds").value) || 2);
    spec.ticks = Math.max(100, Number($input("exp-ticks").value) || 1000);
    // About 60 points per trajectory regardless of run length.
    spec.sampleInterval = Math.max(10, Math.round(spec.ticks / 60));
    return spec;
  }

  private run() {
    const spec = this.resolveSpec();
    this.worker?.terminate();
    this.worker = new Worker(new URL("./experiment.worker.ts", import.meta.url), { type: "module" });
    const prog = $("exp-progress");
    const bar = prog.querySelector(".bar") as HTMLElement;
    const label = prog.querySelector("span") as HTMLElement;
    prog.hidden = false;
    bar.style.width = "0%";
    label.textContent = "Starting…";
    $("exp-run").setAttribute("disabled", "");
    $("exp-cancel").hidden = false;
    const finish = () => {
      $("exp-run").removeAttribute("disabled");
      $("exp-cancel").hidden = true;
    };
    this.worker.onmessage = (e) => {
      const m = e.data;
      if (m.type === "progress") {
        bar.style.width = `${(100 * m.done) / m.total}%`;
        const eta = m.done ? (m.elapsed / m.done) * (m.total - m.done) : 0;
        label.textContent = `${m.done} of ${m.total} runs, ${m.elapsed.toFixed(0)} s elapsed, about ${eta.toFixed(0)} s left`;
      } else if (m.type === "done" || m.type === "cancelled") {
        finish();
        label.textContent = m.type === "done"
          ? `Finished ${m.summary.completed} runs in ${m.elapsed.toFixed(0)} s`
          : `Cancelled after ${m.summary?.completed ?? 0} runs; partial results below`;
        if (m.summary && m.summary.completed > 0) this.showResults(m.summary);
      } else if (m.type === "error") {
        finish();
        label.textContent = `Experiment failed: ${m.message}`;
      }
    };
    this.worker.postMessage({ type: "run", baseUrl: this.baseUrl, spec });
  }

  private showResults(summary: any) {
    this.summary = summary;
    $("exp-results").hidden = false;
    const ts = summary.conditions[0].timeseries;
    const cols = Object.keys(ts).filter((k) => k !== "tick");
    const preferred = ["population", "prey", "predators", "diversity", "food_fraction", "mean_generation"];
    const ordered = [...preferred.filter((c) => cols.includes(c)), ...cols.filter((c) => !preferred.includes(c))];
    const msel = $select("exp-metric");
    const prev = msel.value;
    msel.innerHTML = ordered.map((c) => `<option value="${c}">${escapeHtml(seriesLabel(c))}</option>`).join("");
    msel.value = ordered.includes(prev) ? prev : "population";
    this.drawChart();
    this.drawTable();
  }

  private drawChart() {
    const s = this.summary;
    if (!s) return;
    const col = $select("exp-metric").value;
    const conds = s.conditions;
    const series = conds.flatMap((c: any, i: number) => {
      const color = COND_COLORS[i % COND_COLORS.length];
      const base = i * 3;
      return [
        { label: c.name, color, width: 2, band: [base + 1, base + 2] as [number, number] },
        { label: `${c.name} lo`, color, width: 0 },
        { label: `${c.name} hi`, color, width: 0 },
      ];
    });
    this.chart?.destroy();
    $("exp-chart").innerHTML = "";
    this.chart = new Chart($("exp-chart"), { series, height: 220, xLabel: "tick", yLabel: seriesLabel(col) });
    const ticks: number[] = conds[0].timeseries.tick;
    const data: (number | null)[][] = [ticks];
    for (const c of conds) {
      const d = c.timeseries[col];
      data.push(d.mean, d.lo, d.hi);
    }
    this.chart.set(data);
  }

  private drawTable() {
    const s = this.summary;
    const names: string[] = Object.keys(s.conditions[0].metrics);
    const cell = (m: any, digits = 2) => (m && m.mean !== null && m.mean !== undefined ? fmt(m.mean, digits) : "–");
    const head = ["extinct", "mean_population", "mean_lifespan", "reproductive_success", "mean_diversity", "max_generation"];
    let html = `<tr><th>Condition</th><th>Runs</th><th>Extinct</th>${head.slice(1).map((h) => `<th>${escapeHtml(metricLabel(h))}</th>`).join("")}</tr>`;
    for (const c of s.conditions) {
      const ci = c.extinctionCI;
      html += `<tr><td>${escapeHtml(c.name)}</td><td>${c.runs}</td><td title="95% Wilson interval ${fmt(ci[0])} to ${fmt(ci[1])}">${c.extinctions}/${c.runs}</td>${head.slice(1).map((h) => `<td>${cell(c.metrics[h])}</td>`).join("")}</tr>`;
    }
    html += `<tr><td colspan="${head.length + 2}" style="padding-top:14px"><label class="inline">Compare outcome <select id="exp-outcome">${names
      .map((n) => `<option value="${n}"${n === this.outcome ? " selected" : ""}>${escapeHtml(metricLabel(n))}</option>`).join("")}</select></label></td></tr>`;
    html += `<tr><th>Condition</th><th>n</th><th>Mean ± SD</th><th>95% CI</th><th>Cohen's d</th><th>p (Welch)</th><th></th></tr>`;
    for (const [i, c] of s.conditions.entries()) {
      const m = c.metrics[this.outcome];
      html += `<tr><td>${escapeHtml(c.name)}</td><td>${m.n}</td><td>${cell(m, 3)}${m.sd !== null ? ` ± ${fmt(m.sd, 3)}` : ""}</td>
        <td>${m.ciLo !== null ? `${fmt(m.ciLo, 3)} to ${fmt(m.ciHi, 3)}` : "–"}</td>
        <td>${i === 0 ? '<span class="dim">baseline</span>' : fmt(m.dVsBaseline)}</td>
        <td>${i === 0 ? "" : m.pVsBaseline === null ? "–" : m.pVsBaseline < 0.001 ? "&lt;0.001" : fmt(m.pVsBaseline, 3)}</td><td></td></tr>`;
    }
    $("exp-table").innerHTML = html;
    ($("exp-outcome") as HTMLSelectElement).addEventListener("change", (e) => {
      this.outcome = (e.target as HTMLSelectElement).value;
      this.drawTable();
    });
    $("exp-footnote").textContent = `${s.seeds} seeds per condition, ${s.ticks} ticks per run, engine ${s.engineVersion}. ` +
      `Each simulation run is one replicate; organisms within a run are not treated as independent samples. ` +
      `d and p compare each condition with ${s.baseline} (Welch's t-test, two-sided, not corrected for the many outcomes compared). ` +
      `These are observed outcomes of this model; the question above is what was asked, not a finding.`;
  }

  private downloadRuns() {
    const s = this.summary;
    if (!s) return;
    const names = Object.keys(s.runs[0].metrics);
    const rows = [["condition", "seed", "ticks_run", "extinction_tick", ...names].join(",")];
    for (const r of s.runs) {
      rows.push([r.condition, r.seed, r.ticksRun, r.extinctionTick ?? "", ...names.map((n) => r.metrics[n] ?? "")].join(","));
    }
    download(`${s.experiment}-runs.csv`, rows.join("\n") + "\n", "text/csv");
  }
}
