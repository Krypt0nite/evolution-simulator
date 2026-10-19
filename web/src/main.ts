// EvoLab browser front end. The C++ engine (WebAssembly) owns all simulation
// state; this file schedules ticks, draws snapshots and forwards commands.
import { Chart, fmt, fmtInt } from "./charts";
import { Engine, loadModule, traitValues, type GeneSpec, type Sample, type StatsPayload, type WorldInfo } from "./engine";
import { ExperimentsPanel } from "./experimentsPanel";
import { renderInspector } from "./inspector";
import { Observer, type Observation } from "./observations";
import { hashColor, PREDATOR, Renderer, type ColorMode } from "./renderer";
import { $, $input, $select, download, escapeHtml, geneLabel, prefGet, prefSet } from "./util";

const BASE_URL = new URL(".", document.baseURI).href;
const PARAMS = new URLSearchParams(location.search);

/** What each live readout measures (also shown as tooltips). */
const GLOSSARY: Record<string, string> = {
  "Prey": "Living prey organisms right now.",
  "Predators": "Living predators right now (\"off\" when this world has none).",
  "Births (recent)": "Organisms born over the last few statistics samples; the window length is shown in ticks.",
  "Deaths (recent)": "Organisms that died (starvation, old age, hazards or predation) over the same window.",
  "Total births": "All births since tick 0.",
  "Total deaths": "All deaths since tick 0.",
  "Mean energy": "Average energy reserve of living organisms, as a share of each one's maximum. Low values mean food is hard to get.",
  "Lifespan at death": "Mean age, in ticks, of the organisms that died in the recent window.",
  "Generation": "Mean generation of living organisms: founders are generation 0, a child is one more than its older parent. \"max\" is the deepest line.",
  "Genetic diversity": "Mean distance between the genomes of up to 160 evenly sampled prey, each gene scaled to its allowed range (0 = all identical).",
  "Food available": "Plant biomass on the plate as a share of the total capacity.",
  "Lineages / clusters": "Founders that still have living descendants / groups of similar genomes (genetic clusters). Clusters show divergence, not separate species.",
  "Seed": "Random seed. The same config and seed reproduce this run exactly, here or in the native CLI.",
  "Temperature offset": "Current global temperature shift, including any seasonal cycle. Organisms pay energy for being far from their thermal optimum gene.",
};

interface Checkpoint { tick: number; bytes: Uint8Array }

const app = {
  engine: null as unknown as Engine,
  renderer: null as unknown as Renderer,
  genes: [] as GeneSpec[],
  world: null as WorldInfo | null,
  presets: new Map<string, any>(),
  presetName: "default",
  configText: "",
  running: false,
  speed: "5" as string,
  samples: [] as Sample[],
  latest: null as StatsPayload | null,
  checkpoints: [] as Checkpoint[],
  checkpointEvery: 1000,
  selectedId: 0,
  follow: false,
  highlightFounder: 0,
  lineageHistory: [] as { tick: number; counts: Map<number, number>; total: number }[],
  compareIndex: -1,
  extinctNotified: false,
  lastUiUpdate: 0,
  lastWorldUpdate: 0,
  msPerTick: 0,
  observer: new Observer(),
  /** While true, observations are logged but not announced (replaying history after a reset or restore). */
  quietObservations: false,
  /** UI refresh interval for statistics and panels, in ms (?ui=N). Never affects the simulation. */
  uiInterval: Math.max(16, Number(PARAMS.get("ui")) || 250),
};

let popChart: Chart, traitChart: Chart, divChart: Chart, timelineChart: Chart;

// ------------------------------------------------------------------ boot
async function boot() {
  const canvas = $("world") as HTMLCanvasElement;
  app.renderer = new Renderer(canvas);
  const module = await loadModule(BASE_URL);
  app.engine = new Engine(module);
  app.genes = app.engine.genes();

  const index = await (await fetch(new URL("configs/index.json", BASE_URL))).json();
  await Promise.all(index.presets.map(async (p: string) => {
    app.presets.set(p, await (await fetch(new URL(`configs/${p}.json`, BASE_URL))).json());
  }));
  const presetSel = $select("preset");
  const presetLabels: Record<string, string> = {
    default: "Default (prey only)",
    predator_prey: "Predators, coevolving",
    predator_prey_ecology: "Predators, prey speed fixed",
    sexual: "Sexual reproduction",
    neural: "Neural controllers",
    climate_shift: "Climate shift scenario",
    thermal_niches: "Thermal niches (large)",
  };
  presetSel.innerHTML = index.presets
    .map((p: string) => `<option value="${p}">${escapeHtml(presetLabels[p] ?? p)}</option>`).join("");
  presetSel.value = "default";

  setupCharts();
  setupControls();
  setupCanvas();
  setupEnvironment();
  setupConfigPanel();
  new ExperimentsPanel(BASE_URL, index.experiments, app.presets).init();

  setupIntro();
  $("glossary").innerHTML = Object.entries(GLOSSARY).map(([k, v]) => `<dt>${escapeHtml(k)}</dt><dd>${escapeHtml(v)}</dd>`).join("");

  const seed = app.presets.get("default").seed ?? 42;
  $input("seed").value = String(seed);
  startFromConfig(JSON.stringify(app.presets.get("default"), null, 2));
  // Refit whenever the plate's box changes size (window resize, layout switch, rotation).
  let lastBox = "";
  new ResizeObserver(() => {
    const r = canvas.getBoundingClientRect();
    const box = `${Math.round(r.width)}x${Math.round(r.height)}`;
    if (box === lastBox) return;
    lastBox = box;
    app.renderer.resize();
    app.renderer.fit();
    drawFrame();
  }).observe($("canvas-wrap"));
  requestAnimationFrame(frame);

  // Read-only hooks for the end-to-end tests (web/e2e); only with ?test=1.
  if (PARAMS.get("test") === "1") {
    (window as any).__evolab = {
      tick: () => app.engine.tick(),
      count: () => app.engine.count(),
      hash: () => app.engine.hash(),
      config: () => app.engine.config(),
      running: () => app.running,
      checkpoints: () => app.checkpoints.map((c) => c.tick),
      observations: () => app.observer.log.map((o) => ({ ...o })),
      /** Screen position (CSS px, relative to the canvas) and id of the i-th living organism. */
      organism: (i: number) => {
        const r = app.engine.render();
        if (i >= r.count) return null;
        const x = r.data[i * r.stride], y = r.data[i * r.stride + 1];
        return { id: r.ids[i * 2], sx: x * app.renderer.scale + app.renderer.ox, sy: y * app.renderer.scale + app.renderer.oy };
      },
    };
  }

  // Auto-start: ?autostart=1 in the URL, or the "start automatically" preference.
  const auto = PARAMS.get("autostart");
  if (auto === "1" || (auto !== "0" && prefGet("autostart") === "1")) setRunning(true);
  else if (PARAMS.get("intro") !== "0" && prefGet("introSeen") !== "1") showIntro(true);
}

// ------------------------------------------------------------------ intro
function setupIntro() {
  $("intro-genes").textContent = String(app.genes.length);
  const close = (run: boolean) => {
    showIntro(false);
    prefSet("introSeen", "1");
    prefSet("autostart", $input("intro-autostart").checked ? "1" : "0");
    if (run) setRunning(true);
  };
  $("intro-start").addEventListener("click", () => close(true));
  $("intro-explore").addEventListener("click", () => close(false));
  $("intro").addEventListener("keydown", (e) => { if (e.key === "Escape") { e.stopPropagation(); close(false); } });
  $("btn-help").addEventListener("click", () => showIntro(true));
}

function showIntro(on: boolean) {
  const el = $("intro");
  el.hidden = !on;
  if (on) {
    $input("intro-autostart").checked = prefGet("autostart") === "1";
    ($("intro-start") as HTMLButtonElement).textContent = app.engine.tick() > 0 ? (app.running ? "Continue" : "Resume evolution") : "Start evolution";
    ($("intro-start") as HTMLButtonElement).focus();
  }
}

// ------------------------------------------------------------------ simulation lifecycle
function startFromConfig(text: string) {
  const cfg = JSON.parse(text);
  const seedVal = Number($input("seed").value);
  if (Number.isFinite(seedVal)) cfg.seed = seedVal;
  const json = JSON.stringify(cfg, null, 2);
  app.engine.create(json);
  app.configText = json;
  onNewState(true);
}

function onNewState(refit: boolean) {
  app.samples = [];
  app.latest = null;
  app.lineageHistory = [];
  app.compareIndex = -1;
  app.extinctNotified = false;
  app.selectedId = 0;
  app.follow = false;
  app.highlightFounder = 0;
  app.renderer.selectedId = 0;
  app.renderer.highlightFounder = 0;
  app.world = app.engine.world();
  $input("seed").value = String(app.world.seed);
  // Narrow layouts size the plate box to the world's aspect ratio (see styles.css).
  $("canvas-wrap").style.setProperty("--plate-aspect", `${app.world.width} / ${app.world.height}`);
  app.renderer.resize();
  app.renderer.setWorld(app.world, refit);
  ($("cfg-text") as HTMLTextAreaElement).value = app.engine.config();
  setBanner("");
  updateStatus();
  // History already in the engine (after a restore) is logged but not announced.
  app.observer.reset();
  app.quietObservations = true;
  pullStats(true);
  app.quietObservations = false;
  renderInspectorPanel();
  updateLegend();
  updateEventLog();
  updateEnvironmentInputs();
  drawFrame();
}

function resetCheckpoints() {
  app.checkpoints = [];
  if (app.checkpointEvery > 0) app.checkpoints.push({ tick: app.engine.tick(), bytes: app.engine.saveCheckpoint() });
}

function stepTicks(n: number): number {
  let done = 0;
  const t0 = performance.now();
  const budget = app.speed === "max" ? 14 : Infinity;
  while (done < n) {
    const before = app.engine.tick();
    const pop = app.engine.step(1);
    done++;
    const tick = app.engine.tick();
    if (app.checkpointEvery > 0 && tick % app.checkpointEvery === 0 && tick !== before) {
      app.checkpoints.push({ tick, bytes: app.engine.saveCheckpoint() });
      if (app.checkpoints.length > 24) app.checkpoints.splice(1, 1);  // keep the first one
    }
    if (pop === 0) break;
    if (performance.now() - t0 > budget) break;
  }
  const elapsed = performance.now() - t0;
  if (done > 0) app.msPerTick = app.msPerTick * 0.8 + (elapsed / done) * 0.2;
  return done;
}

function frame(now: number) {
  if (app.running) {
    const n = app.speed === "max" ? 100000 : Number(app.speed);
    stepTicks(n);
    if (app.engine.count() === 0) setRunning(false);
  }
  if (app.follow && app.selectedId) {
    const r = app.engine.render();
    for (let i = 0; i < r.count; i++) {
      if (r.ids[i * 2] === app.selectedId) {
        app.renderer.centerOn(r.data[i * r.stride], r.data[i * r.stride + 1]);
        break;
      }
    }
  }
  drawFrame();
  if (now - app.lastWorldUpdate > 500) {
    app.lastWorldUpdate = now;
    app.world = app.engine.world();
    app.renderer.setWorld(app.world, false);
  }
  if (now - app.lastUiUpdate > app.uiInterval) {
    app.lastUiUpdate = now;
    updateStatus();
    pullStats(false);
    if (activeTab() === "inspect" && app.selectedId) renderInspectorPanel();
  }
  requestAnimationFrame(frame);
}

function drawFrame() {
  if (!app.world) return;
  app.renderer.updateFood(app.engine.food(app.world.foodCols * app.world.foodRows));
  app.renderer.draw(app.engine.render());
}

function setRunning(on: boolean) {
  app.running = on;
  const b = $("btn-run");
  b.textContent = on ? "Pause" : app.engine.tick() > 0 ? "Resume" : "Run";
  if (on && app.checkpoints.length === 0) resetCheckpoints();
  updateStatus();
}

function updateStatus() {
  const tick = app.engine.tick(), count = app.engine.count();
  $("st-tick").textContent = fmtInt(tick);
  $("st-pop").textContent = fmtInt(count);
  $("st-ms").textContent = app.msPerTick ? app.msPerTick.toFixed(2) : "–";
  // A visible call to action whenever the simulation is paused, so a still plate never looks broken.
  const cta = $("paused-cta");
  cta.hidden = app.running || count === 0;
  if (!cta.hidden) {
    $("paused-text").textContent = tick === 0
      ? `Ready: ${fmtInt(count)} founder organisms with random genomes.`
      : `Paused at tick ${fmtInt(tick)}.`;
    $("cta-run").textContent = tick === 0 ? "Start evolution" : "Resume";
  }
  if (!app.running) $("btn-run").textContent = tick > 0 ? "Resume" : "Run";
  $("state-line").textContent = describeState();
}

/** One plain sentence about the current state: only measured facts, no interpretation. */
function describeState(): string {
  const c = app.latest?.current;
  const tick = app.engine.tick(), count = app.engine.count();
  const state = app.running ? "Running" : tick === 0 ? "Ready" : "Paused";
  if (count === 0) return `Extinct: no organisms alive at tick ${fmtInt(tick)}. Reset, try a new seed, or restore a checkpoint.`;
  if (!c) return `${state} at tick ${fmtInt(tick)}.`;
  const who = c.predators > 0 ? `${fmtInt(c.prey)} prey and ${fmtInt(c.predators)} predators` : `${fmtInt(c.prey)} organisms`;
  const lineages = `${fmtInt(c.lineages)} founder lineage${c.lineages === 1 ? "" : "s"}`;
  if (tick === 0) return `${state}: ${who} from ${lineages}, none born yet. Food at ${Math.round(c.foodFraction * 100)}% of capacity.`;
  return `${state} · tick ${fmtInt(tick)}: ${who} from ${lineages}, mean generation ${fmt(c.meanGeneration, 1)}. ` +
    `Food at ${Math.round(c.foodFraction * 100)}% of capacity.`;
}

// ------------------------------------------------------------------ observations and notifications
function handleObservations(s: StatsPayload) {
  const fresh = app.observer.update(app.samples, s);
  if (fresh.length && !app.quietObservations) for (const o of fresh) toast(o);
  const log = app.observer.log;
  $("obs-log").innerHTML = log.length
    ? [...log].reverse().map((o) => `<li class="obs-${o.kind}"><b>Tick ${fmtInt(o.tick)}</b> ${escapeHtml(o.title)}. <span>${escapeHtml(o.detail)}</span></li>`).join("")
    : `<li class="none">Nothing notable yet. Extinctions, crashes, recoveries, predators arriving or dying out, and new genetic clusters are listed here.</li>`;
}

function toast(o: Observation) {
  const box = $("toasts");
  const el = document.createElement("div");
  el.className = `toast${o.danger ? " danger" : ""}`;
  el.setAttribute("role", "status");
  el.innerHTML = `<div><b>${escapeHtml(o.title)}</b><span class="t-tick">tick ${fmtInt(o.tick)}</span></div><p>${escapeHtml(o.detail)}</p>`;
  const close = document.createElement("button");
  close.className = "toast-close";
  close.setAttribute("aria-label", "Dismiss");
  close.textContent = "×";
  close.addEventListener("click", () => el.remove());
  el.appendChild(close);
  box.appendChild(el);
  while (box.children.length > 3) box.firstElementChild!.remove();
  setTimeout(() => el.remove(), o.danger ? 15000 : 8000);
}

function updateTraitChange() {
  const s = app.samples;
  const first = s.find((x) => x.prey > 0), last = app.latest?.current;
  const el = $("trait-change");
  if (!first || !last || last.prey === 0 || last.tick === first.tick) {
    el.innerHTML = `<tr><td class="dim">Appears once the simulation has run.</td></tr>`;
    return;
  }
  const genes = ["speed", "vision", "size", "metabolism", "maturity", "fertility", "riskTolerance", "social", "thermalOptimum"];
  const rows = genes.filter((g) => g in last.traitMean).map((g) => {
    const a = first.traitMean[g], b = last.traitMean[g], sd0 = first.traitSd[g];
    const z = sd0 > 1e-9 ? (b - a) / sd0 : NaN;
    return `<tr><td>${escapeHtml(geneLabel(g))}</td><td>${fmt(a)}</td><td>${fmt(b)}</td><td>${(b - a >= 0 ? "+" : "") + fmt(b - a)}</td><td>${Number.isFinite(z) ? (z >= 0 ? "+" : "") + z.toFixed(1) : "–"}</td></tr>`;
  });
  el.innerHTML = `<tr><th>Prey mean</th><th>Tick ${fmtInt(first.tick)}</th><th>Now</th><th>Change</th><th title="Change divided by the standard deviation among the starting population">in start SDs</th></tr>${rows.join("")}`;
}

function setBanner(text: string, danger = false) {
  const b = $("banner");
  b.hidden = !text;
  b.textContent = text;
  b.classList.toggle("danger", danger);
}

// ------------------------------------------------------------------ statistics
function pullStats(full: boolean) {
  const from = full ? 0 : app.samples.length;
  const s = app.engine.stats(from);
  if (full || s.total < app.samples.length) app.samples = [];
  if (s.total < from) {
    // Engine thinned or reset its history: re-read everything.
    app.samples = app.engine.stats(0).samples;
  } else {
    app.samples.push(...s.samples);
  }
  app.latest = s;
  const counts = new Map<number, number>();
  for (const l of s.lineages) counts.set(l.founder, l.alive);
  const last = app.lineageHistory.at(-1);
  if (!last || last.tick !== s.current.tick) {
    app.lineageHistory.push({ tick: s.current.tick, counts, total: s.current.population });
    if (app.lineageHistory.length > 400) app.lineageHistory = app.lineageHistory.filter((_, i) => i % 2 === 0);
  }
  updateReadout(s);
  updateCharts();
  updateHistogram();
  drawMuller();
  updateClusterTable(s);
  updateLedger(s);
  checkExtinction(s.current);
  handleObservations(s);
  updateTraitChange();
  $("awaiting").hidden = app.samples.length > 1;
  $("state-line").textContent = describeState();
}

function checkExtinction(c: Sample) {
  if (c.population === 0) {
    if (!app.extinctNotified) {
      setBanner(`The population went extinct at tick ${fmtInt(c.tick)}. Reset, try a new seed, or restore a checkpoint from the timeline.`, true);
      app.extinctNotified = true;
    }
  } else if (c.prey > 0 && c.prey < 15) {
    setBanner(`Prey population is critically low (${c.prey}). Extinction is likely without a change in conditions.`);
  } else if (app.world?.predatorsEnabled && c.predators > 0 && c.predators < 5 && c.tick > 0) {
    setBanner(`Only ${c.predators} predators remain.`);
  } else if (!app.extinctNotified) {
    setBanner("");
  }
}

function updateReadout(s: StatsPayload) {
  const c = s.current;
  const recent = app.samples.slice(-4);
  const deathAge = recent.filter((x) => x.deaths > 0);
  const meanAgeAtDeath = deathAge.length ? deathAge.reduce((a, x) => a + x.meanAgeAtDeath * x.deaths, 0) / deathAge.reduce((a, x) => a + x.deaths, 0) : NaN;
  const births = recent.reduce((a, x) => a + x.births, 0), deaths = recent.reduce((a, x) => a + x.deaths, 0);
  const span = recent.length > 1 ? recent.at(-1)!.tick - recent[0].tick + (recent[1].tick - recent[0].tick) : 0;
  const rows: [string, string][] = [
    ["Prey", fmt(c.prey, 0)],
    ["Predators", app.world?.predatorsEnabled ? fmt(c.predators, 0) : "off"],
    ["Births (recent)", span ? `${births}<small>/${span} ticks</small>` : "–"],
    ["Deaths (recent)", span ? `${deaths}<small>/${span} ticks</small>` : "–"],
    ["Total births", fmtInt(s.totalBirths)],
    ["Total deaths", fmtInt(s.totalDeaths)],
    ["Mean energy", `${Math.round(c.meanEnergyFrac * 100)}<small>% full</small>`],
    ["Lifespan at death", Number.isFinite(meanAgeAtDeath) ? `${fmt(meanAgeAtDeath, 0)}<small>ticks</small>` : "–"],
    ["Generation", `${fmt(c.meanGeneration, 1)}<small>max ${c.maxGeneration}</small>`],
    ["Genetic diversity", fmt(c.diversity, 3)],
    ["Food available", `${Math.round(c.foodFraction * 100)}<small>%</small>`],
    ["Lineages / clusters", `${c.lineages}<small>/ ${c.clusters}</small>`],
    ["Seed", String(app.world?.seed ?? "")],
    ["Temperature offset", fmt(c.temperatureOffset)],
  ];
  $("readout").innerHTML = rows.map(([k, v]) => `<div title="${escapeHtml(GLOSSARY[k] ?? "")}"><dt>${k}</dt><dd>${v}</dd></div>`).join("");
}

function setupCharts() {
  const traitSel = $select("trait-select"), tlSel = $select("tl-trait");
  const opts = app.genes.map((g) => `<option value="${g.name}">${escapeHtml(geneLabel(g.name))}</option>`).join("");
  traitSel.innerHTML = opts;
  tlSel.innerHTML = opts;
  traitSel.value = "speed";
  tlSel.value = "size";
  traitSel.addEventListener("change", () => { updateCharts(); updateHistogram(); });
  tlSel.addEventListener("change", () => { rebuildTimeline(); updateCharts(); });

  popChart = new Chart($("chart-pop"), {
    height: 150, yLabel: "organisms",
    series: [{ label: "Prey", color: "#4cc9e0" }, { label: "Predators", color: PREDATOR }],
  });
  traitChart = new Chart($("chart-trait"), {
    height: 150,
    series: [{ label: "Mean", color: "#f0b54a", width: 2, band: [1, 2] }, { label: "Mean lo", color: "#f0b54a", width: 0 }, { label: "Mean hi", color: "#f0b54a", width: 0 }],
  });
  divChart = new Chart($("chart-div"), {
    height: 140,
    series: [{ label: "Diversity ×10", color: "#a191ff" }, { label: "Food available", color: "#6bd39e" }],
    yRange: [0, 1.05],
  });
  rebuildTimeline();
}

function rebuildTimeline() {
  timelineChart?.destroy();
  $("chart-timeline").innerHTML = "";
  const g = $select("tl-trait").value;
  timelineChart = new Chart($("chart-timeline"), {
    height: 128,
    yLabel: "organisms",
    rightAxis: { label: geneLabel(g) },
    series: [
      { label: "Population", color: "#4cc9e0" },
      { label: `${geneLabel(g)} (mean)`, color: "#f0b54a", scale: "right" },
    ],
    markers: () => app.checkpoints.map((c) => c.tick),
    onClick: (idx) => { app.compareIndex = idx; renderCompare(); },
  });
  updateCharts();
}

function updateCharts() {
  const s = app.samples;
  if (!s.length || !popChart) return;
  const ticks = s.map((x) => x.tick);
  popChart.set([ticks, s.map((x) => x.prey), s.map((x) => (app.world?.predatorsEnabled ? x.predators : null))]);
  const g = $select("trait-select").value;
  traitChart.set([
    ticks,
    s.map((x) => (x.prey ? x.traitMean[g] : null)),
    s.map((x) => (x.prey ? x.traitMean[g] - x.traitSd[g] : null)),
    s.map((x) => (x.prey ? x.traitMean[g] + x.traitSd[g] : null)),
  ]);
  divChart.set([ticks, s.map((x) => Math.min(1.05, x.diversity * 10)), s.map((x) => x.foodFraction)]);
  const tg = $select("tl-trait").value;
  timelineChart.set([ticks, s.map((x) => x.population), s.map((x) => (x.prey ? x.traitMean[tg] : null))]);
  if (app.compareIndex >= 0) renderCompare();
}

function updateHistogram() {
  const canvas = $("hist") as HTMLCanvasElement;
  const ctx = canvas.getContext("2d")!;
  const dpr = Math.min(2, devicePixelRatio || 1);
  const w = canvas.clientWidth, h = 110;
  canvas.width = w * dpr;
  canvas.height = h * dpr;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  const gi = app.genes.findIndex((x) => x.name === $select("trait-select").value);
  if (gi < 0 || app.engine.count() === 0) return;
  const spec = app.genes[gi];
  const vals = traitValues(app.engine, gi);
  const render = app.engine.render();
  const bins = 30;
  const prey = new Array(bins).fill(0), pred = new Array(bins).fill(0);
  for (let i = 0; i < vals.length; i++) {
    const b = Math.min(bins - 1, Math.max(0, Math.floor(((vals[i] - spec.min) / (spec.max - spec.min)) * bins)));
    (render.data[i * render.stride + 6] === 1 ? pred : prey)[b]++;
  }
  const max = Math.max(1, ...prey, ...pred);
  const pad = 16, bw = (w - 2) / bins;
  for (let b = 0; b < bins; b++) {
    const hp = ((h - pad - 4) * prey[b]) / max;
    ctx.fillStyle = "#4cc9e0";
    ctx.fillRect(1 + b * bw + 1, h - pad - hp, bw - 2, hp);
    if (pred[b]) {
      const hq = ((h - pad - 4) * pred[b]) / max;
      ctx.fillStyle = "rgba(238,110,107,0.8)";
      ctx.fillRect(1 + b * bw + bw * 0.3, h - pad - hq, bw * 0.4, hq);
    }
  }
  ctx.fillStyle = "#7c90ab";
  ctx.font = "11px 'Instrument Sans', system-ui";
  ctx.textBaseline = "bottom";
  ctx.fillText(fmt(spec.min), 4, h - 1);
  const maxLabel = fmt(spec.max);
  ctx.fillText(maxLabel, w - 4 - ctx.measureText(maxLabel).width, h - 1);
  const label = `${geneLabel(spec.name)}, ${vals.length} organisms`;
  ctx.fillText(label, (w - ctx.measureText(label).width) / 2, h - 1);
}

// Stacked lineage shares over time (a simple Muller-style plot).
const mullerBands: { founder: number; path: Path2D }[] = [];
function drawMuller() {
  const canvas = $("muller") as HTMLCanvasElement;
  const ctx = canvas.getContext("2d")!;
  const dpr = Math.min(2, devicePixelRatio || 1);
  const w = canvas.clientWidth, h = 120;
  canvas.width = w * dpr;
  canvas.height = h * dpr;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  mullerBands.length = 0;
  const hist = app.lineageHistory;
  const list = $("lineage-list");
  if (hist.length < 2) { list.innerHTML = ""; return; }
  const last = hist.at(-1)!;
  const top = [...last.counts.entries()].sort((a, b) => b[1] - a[1]).slice(0, 8).map(([f]) => f);
  const t0 = hist[0].tick, t1 = last.tick || 1;
  const x = (t: number) => ((t - t0) / Math.max(1, t1 - t0)) * w;
  const lower = new Array(hist.length).fill(0);
  for (const f of top) {
    const upper = hist.map((e, i) => lower[i] + (e.total ? (e.counts.get(f) ?? 0) / e.total : 0));
    const p = new Path2D();
    hist.forEach((e, i) => (i ? p.lineTo(x(e.tick), h - upper[i] * h) : p.moveTo(x(e.tick), h - upper[i] * h)));
    for (let i = hist.length - 1; i >= 0; i--) p.lineTo(x(hist[i].tick), h - lower[i] * h);
    p.closePath();
    ctx.globalAlpha = app.highlightFounder && app.highlightFounder !== f ? 0.25 : 0.9;
    ctx.fillStyle = hashColor(f);
    ctx.fill(p);
    mullerBands.push({ founder: f, path: p });
    upper.forEach((v, i) => (lower[i] = v));
  }
  ctx.globalAlpha = 1;
  list.innerHTML = top.map((f) => `<li data-founder="${f}" class="${app.highlightFounder === f ? "on" : ""}"><i style="background:${hashColor(f)}"></i>Founder #${f}<b>${last.counts.get(f)}</b></li>`).join("");
  list.querySelectorAll("li").forEach((li) => li.addEventListener("click", () => setHighlight(Number((li as HTMLElement).dataset.founder))));
}

function updateClusterTable(s: StatsPayload) {
  const show = ["speed", "vision", "size", "maturity", "thermalOptimum"];
  const rows = s.clusters.slice(0, 8).map((c) => `<tr><td>${c.id}</td><td>${c.size}</td><td>${c.predators ? "predators" : "prey"}</td>${show.map((g) => `<td>${fmt(c.centroid[g])}</td>`).join("")}</tr>`);
  $("cluster-table").innerHTML = `<tr><th>Cluster</th><th>Size</th><th>Kind</th>${show.map((g) => `<th>${escapeHtml(geneLabel(g))}</th>`).join("")}</tr>${rows.join("")}`;
}

function updateLedger(s: StatsPayload) {
  const L = s.ledger;
  const rows: [string, number, string][] = [
    ["Plant growth (inflow)", L.inflow, "in"], ["Organisms added", L.introduced, "in"],
    ["Maintenance", L.basal, "out"], ["Movement", L.movement, "out"], ["Temperature stress", L.thermal, "out"],
    ["Digestion losses", L.digestion, "out"], ["Reproduction overhead", L.reproduction, "out"],
    ["Hazards", L.hazard, "out"], ["Hunting effort", L.hunting, "out"], ["Left in carcasses", L.carcass, "out"],
    ["Plant decay and depletion", L.decay + L.depleted + L.overflow, "out"],
  ];
  $("ledger-table").innerHTML = `<tr><th>Flow since tick 0</th><th>Energy</th><th></th></tr>` +
    rows.filter((r) => r[1] > 0).map(([k, v, d]) => `<tr><td>${k}</td><td>${fmt(v, 0)}</td><td class="dim">${d}</td></tr>`).join("") +
    `<tr><td>Plants assimilated by prey</td><td>${fmt(L.assimilatedPlants, 0)}</td><td class="dim">transfer</td></tr>` +
    (L.assimilatedPrey > 0 ? `<tr><td>Prey assimilated by predators</td><td>${fmt(L.assimilatedPrey, 0)}</td><td class="dim">transfer</td></tr>` : "");
}

// ------------------------------------------------------------------ timeline compare / rollback
let compareTableHtml = "";
let compareActionTick = -2;
let compareCheckpoint: Checkpoint | null = null;

function renderCompare() {
  const el = $("tl-compare");
  const s = app.samples[app.compareIndex];
  const now = app.latest?.current;
  if (!s || !now) {
    el.innerHTML = `<p class="empty">Select a moment on the timeline.</p>`;
    compareTableHtml = "";
    compareActionTick = -2;
    return;
  }
  if (!el.querySelector("#tl-table")) {
    el.innerHTML = `<div id="tl-table"></div><div id="tl-actions"></div>`;
    compareTableHtml = "";
    compareActionTick = -2;
  }
  const ckpt = [...app.checkpoints].reverse().find((c) => c.tick <= s.tick) ?? null;
  const rows: [string, string, string][] = [
    ["Population", fmt(s.population, 0), fmt(now.population, 0)],
    ["Generation (mean)", fmt(s.meanGeneration, 1), fmt(now.meanGeneration, 1)],
    ["Diversity", fmt(s.diversity, 3), fmt(now.diversity, 3)],
    ["Food available", fmt(s.foodFraction), fmt(now.foodFraction)],
    ...["speed", "vision", "size", "metabolism", "maturity", "fertility", "thermalOptimum"].map((g): [string, string, string] =>
      [geneLabel(g), fmt(s.traitMean[g]), fmt(now.traitMean[g])]),
  ];
  const table = `<table class="table"><tr><th></th><th>Tick ${fmtInt(s.tick)}</th><th>Now (${fmtInt(now.tick)})</th></tr>
    ${rows.map(([k, a, b]) => `<tr><td>${k}</td><td>${a}</td><td>${b}</td></tr>`).join("")}</table>`;
  if (table !== compareTableHtml) {
    compareTableHtml = table;
    $("tl-table").innerHTML = table;
  }
  // The action area only changes when the relevant checkpoint changes, so the
  // restore button is never replaced while someone is clicking it.
  const ckTick = ckpt ? ckpt.tick : -1;
  compareCheckpoint = ckpt;
  if (ckTick !== compareActionTick) {
    compareActionTick = ckTick;
    $("tl-actions").innerHTML = `<p class="note" style="margin-top:6px">${ckpt
      ? `Nearest checkpoint at or before this moment: tick ${fmtInt(ckpt.tick)}.`
      : "No checkpoint at or before this moment; it can be inspected but not restored."}</p>
      ${ckpt ? `<button id="tl-restore">Restore tick ${fmtInt(ckpt.tick)}</button>` : ""}`;
  }
}

function restoreCheckpoint(ckpt: Checkpoint) {
  setRunning(false);
  app.engine.loadCheckpoint(ckpt.bytes);
  // Later checkpoints belong to the abandoned future; drop them.
  app.checkpoints = app.checkpoints.filter((c) => c.tick <= ckpt.tick);
  onNewState(false);
  setBanner(`Restored the complete simulation state at tick ${fmtInt(ckpt.tick)}. Running again creates a new branch from here.`);
}

// ------------------------------------------------------------------ inspector / selection
function select(id: number) {
  app.selectedId = id;
  app.renderer.selectedId = id;
  setTab("inspect");
  renderInspectorPanel();
}

function setHighlight(founder: number) {
  app.highlightFounder = founder;
  app.renderer.highlightFounder = founder;
  drawMuller();
  renderInspectorPanel();
}

function renderInspectorPanel() {
  const el = $("inspector");
  if (!app.selectedId) {
    el.innerHTML = `<p class="empty">Click an organism on the plate to inspect its genome, behaviour and family.</p>`;
    return;
  }
  const o = app.engine.inspect(app.selectedId);
  if (o && !o.alive && app.follow) app.follow = false;
  renderInspector(el, o, app.genes, app.latest?.current.traitMean ?? null,
    { follow: app.follow, highlightFounder: app.highlightFounder },
    {
      select,
      setFollow: (on) => { app.follow = on; renderInspectorPanel(); },
      setHighlight,
    });
}

// ------------------------------------------------------------------ controls
function activeTab(): string {
  return (document.querySelector(".tabs [aria-selected=true]") as HTMLElement)?.dataset.tab ?? "live";
}

function setTab(name: string) {
  document.querySelectorAll<HTMLButtonElement>(".tabs button").forEach((b) => b.setAttribute("aria-selected", String(b.dataset.tab === name)));
  document.querySelectorAll<HTMLElement>(".panel").forEach((p) => (p.hidden = p.dataset.panel !== name));
  if (name === "live") { updateCharts(); updateHistogram(); drawMuller(); }
}

function setupControls() {
  $("tl-compare").addEventListener("click", (e) => {
    if ((e.target as HTMLElement).closest("#tl-restore") && compareCheckpoint) restoreCheckpoint(compareCheckpoint);
  });
  document.querySelectorAll<HTMLButtonElement>(".tabs button").forEach((b) => b.addEventListener("click", () => setTab(b.dataset.tab!)));
  $("btn-run").addEventListener("click", () => setRunning(!app.running));
  $("cta-run").addEventListener("click", () => setRunning(true));
  $("btn-fit").addEventListener("click", () => { app.follow = false; app.renderer.fit(); });
  $("btn-more").addEventListener("click", () => {
    const open = document.body.classList.toggle("controls-open");
    $("btn-more").setAttribute("aria-expanded", String(open));
  });
  $("btn-step").addEventListener("click", () => { setRunning(false); stepTicks(1); updateStatus(); pullStats(false); renderInspectorPanel(); });
  $("btn-reset").addEventListener("click", () => {
    setRunning(false);
    app.checkpoints = [];
    startFromConfig(app.configText);
  });
  $("btn-newseed").addEventListener("click", () => {
    setRunning(false);
    $input("seed").value = String(Math.floor(Math.random() * 1e9));
    app.checkpoints = [];
    startFromConfig(app.configText);
  });
  $input("seed").addEventListener("change", () => { setRunning(false); app.checkpoints = []; startFromConfig(app.configText); });
  $select("speed").addEventListener("change", (e) => (app.speed = (e.target as HTMLSelectElement).value));
  $select("preset").addEventListener("change", (e) => {
    setRunning(false);
    app.presetName = (e.target as HTMLSelectElement).value;
    const cfg = app.presets.get(app.presetName);
    $input("seed").value = String(cfg.seed ?? 42);
    app.checkpoints = [];
    startFromConfig(JSON.stringify(cfg, null, 2));
  });
  window.addEventListener("keydown", (e) => {
    const t = e.target as HTMLElement;
    if (t.matches("input, textarea, select") || !$("intro").hidden) return;
    if (e.code === "Space") { e.preventDefault(); setRunning(!app.running); }
    else if (e.key === ".") { setRunning(false); stepTicks(1); updateStatus(); }
    else if (e.key === "f" || e.key === "F") app.renderer.fit();
    else if (e.key === "Escape") { app.renderer.pendingTool = null; select(0); setTab("live"); }
  });
}

function setupCanvas() {
  const canvas = $("world") as HTMLCanvasElement;
  const r = app.renderer;
  let drag: { x: number; y: number; ox: number; oy: number; moved: boolean } | null = null;
  // Active touch/pen/mouse pointers, for two-finger pinch zoom.
  const pointers = new Map<number, { x: number; y: number }>();
  let pinch: { dist: number } | null = null;
  const pinchState = () => {
    const [a, b] = [...pointers.values()];
    return { dist: Math.hypot(a.x - b.x, a.y - b.y), mx: (a.x + b.x) / 2, my: (a.y + b.y) / 2 };
  };
  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    const rect = canvas.getBoundingClientRect();
    r.zoomAt(e.clientX - rect.left, e.clientY - rect.top, Math.exp(-e.deltaY * 0.0015));
  }, { passive: false });
  canvas.addEventListener("pointerdown", (e) => {
    canvas.setPointerCapture(e.pointerId);
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pointers.size === 2) {
      pinch = { dist: pinchState().dist };
      drag = null;  // a pinch is never a tap or a pan
      app.follow = false;
      return;
    }
    drag = { x: e.clientX, y: e.clientY, ox: r.ox, oy: r.oy, moved: false };
  });
  const endPointer = (e: PointerEvent) => {
    pointers.delete(e.pointerId);
    if (pointers.size < 2) pinch = null;
  };
  canvas.addEventListener("pointercancel", (e) => { endPointer(e); drag = null; canvas.classList.remove("panning"); });
  canvas.addEventListener("pointermove", (e) => {
    const rect = canvas.getBoundingClientRect();
    r.pointer = r.toWorld(e.clientX - rect.left, e.clientY - rect.top);
    if (pointers.has(e.pointerId)) pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pinch && pointers.size === 2) {
      const p = pinchState();
      if (pinch.dist > 0 && p.dist > 0) r.zoomAt(p.mx - rect.left, p.my - rect.top, p.dist / pinch.dist);
      pinch.dist = p.dist;
      return;
    }
    if (!drag) return;
    const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
    if (Math.abs(dx) + Math.abs(dy) > 4) drag.moved = true;
    if (drag.moved) {
      canvas.classList.add("panning");
      app.follow = false;
      r.ox = drag.ox + dx;
      r.oy = drag.oy + dy;
    }
  });
  canvas.addEventListener("pointerleave", () => (r.pointer = null));
  canvas.addEventListener("pointerup", (e) => {
    const wasPinch = pinch !== null || pointers.size > 1;
    endPointer(e);
    canvas.classList.remove("panning");
    if (wasPinch) { drag = null; return; }
    const wasDrag = drag?.moved;
    drag = null;
    if (wasDrag) return;
    const rect = canvas.getBoundingClientRect();
    const p = r.toWorld(e.clientX - rect.left, e.clientY - rect.top);
    if (r.pendingTool) {
      const tool = r.pendingTool;
      const ev: Record<string, unknown> = { type: tool.kind, x: p.x, y: p.y, radius: tool.radius };
      if (tool.kind === "addHazard") ev.damage = Number($input("env-hz-d").value);
      applyEvent(ev);
      r.pendingTool = null;
      document.querySelectorAll("[data-tool]").forEach((b) => b.classList.remove("active"));
      return;
    }
    const id = app.engine.pick(p.x, p.y, Math.max(3, 10 / r.scale));
    if (id) select(id);
  });
}

// ------------------------------------------------------------------ environment
function applyEvent(ev: Record<string, unknown>) {
  try {
    app.engine.scheduleEvent(ev);
    setBanner("");
    updateEventLog();
    if (!app.running) {
      // Apply immediately so the change is visible while paused.
      stepTicks(1);
      updateStatus();
      pullStats(false);
    }
    app.world = app.engine.world();
    app.renderer.setWorld(app.world, false);
    updateEnvironmentInputs();
  } catch (err) {
    setBanner(`Could not apply the change: ${(err as Error).message}`, true);
  }
}

function setupEnvironment() {
  const bind = (inputId: string, outId: string, f: (v: number) => string) => {
    const i = $input(inputId), o = $(outId);
    const upd = () => (o.textContent = f(Number(i.value)));
    i.addEventListener("input", upd);
    upd();
  };
  bind("env-food", "out-food", (v) => `${v.toFixed(2)}×`);
  bind("env-regrow", "out-regrow", (v) => v.toFixed(3));
  bind("env-temp", "out-temp", (v) => (v > 0 ? "+" : "") + v.toFixed(2));
  bind("env-hz-r", "out-hz-r", (v) => v.toFixed(0));
  bind("env-hz-d", "out-hz-d", (v) => v.toFixed(2));
  bind("env-dep-r", "out-dep-r", (v) => v.toFixed(0));

  document.querySelectorAll<HTMLButtonElement>("[data-env]").forEach((b) => b.addEventListener("click", () => {
    const type = b.dataset.env!;
    const ev: Record<string, unknown> = { type };
    if (type === "foodMultiplier") ev.value = Number($input("env-food").value);
    if (type === "setRegrowth") ev.value = Number($input("env-regrow").value);
    if (type === "temperatureShift") ev.value = Number($input("env-temp").value);
    if (type === "introducePredators") ev.count = Number($input("env-pred").value);
    if (type === "removeRandom") ev.count = Number($input("env-cull").value);
    applyEvent(ev);
  }));
  document.querySelectorAll<HTMLButtonElement>("[data-tool]").forEach((b) => b.addEventListener("click", () => {
    const kind = b.dataset.tool!;
    const active = app.renderer.pendingTool?.kind === kind;
    document.querySelectorAll("[data-tool]").forEach((x) => x.classList.remove("active"));
    if (active) { app.renderer.pendingTool = null; return; }
    b.classList.add("active");
    const radius = Number($input(kind === "addHazard" ? "env-hz-r" : "env-dep-r").value);
    app.renderer.pendingTool = { kind, radius };
    setBanner(kind === "addHazard" ? "Click on the plate to place the hazard. Esc cancels." : "Click on the plate to remove all food in that area. Esc cancels.");
  }));
  const layer = (id: string, key: keyof Renderer["layers"]) =>
    $input(id).addEventListener("change", (e) => { app.renderer.layers[key] = (e.target as HTMLInputElement).checked; });
  layer("lyr-food", "food");
  layer("lyr-temperature", "temperature");
  layer("lyr-hazards", "hazards");
  layer("lyr-vision", "vision");
  layer("lyr-grid", "grid");
  $select("color-mode").addEventListener("change", (e) => {
    app.renderer.colorMode = (e.target as HTMLSelectElement).value as ColorMode;
    updateLegend();
  });
}

function updateEnvironmentInputs() {
  if (!app.world) return;
  const set = (id: string, v: number) => { const i = $input(id); i.value = String(v); i.dispatchEvent(new Event("input")); };
  set("env-food", app.world.foodMultiplier);
  set("env-regrow", app.world.regrowthRate);
  set("env-temp", app.world.temperatureOffset);
}

function updateEventLog() {
  const cfg = JSON.parse(app.engine.config());
  const evs: any[] = cfg.environment.events ?? [];
  const describe = (e: any) => {
    switch (e.type) {
      case "foodMultiplier": return `food capacity set to ${fmt(e.value)}×`;
      case "temperatureShift": return `temperature offset set to ${fmt(e.value)}`;
      case "addHazard": return `hazard placed at (${fmt(e.x, 0)}, ${fmt(e.y, 0)}), radius ${fmt(e.radius, 0)}, drain ${fmt(e.damage)}`;
      case "clearHazards": return "all hazards cleared";
      case "shiftResources": return "food patches relocated";
      case "introducePredators": return `${e.count} predators introduced`;
      case "setRegrowth": return `regrowth rate set to ${fmt(e.value, 3)}`;
      case "removeRandom": return `${e.count} organisms removed at random`;
      case "depleteRegion": return `food removed around (${fmt(e.x, 0)}, ${fmt(e.y, 0)}), radius ${fmt(e.radius, 0)}`;
      default: return e.type;
    }
  };
  const tick = app.engine.tick();
  $("event-log").innerHTML = evs.length
    ? evs.map((e) => `<li><b>Tick ${fmtInt(Number(e.tick))}</b>: ${escapeHtml(describe(e))}${e.tick > tick ? " (scheduled)" : ""}</li>`).join("")
    : `<li>No changes yet.</li>`;
}

function updateLegend() {
  const mode = app.renderer.colorMode;
  const lines: string[] = [];
  if (mode === "lineage") lines.push("Colour: lineage (founder)");
  if (mode === "cluster") lines.push("Colour: genetic cluster");
  if (mode === "energy") lines.push(`<i style="background:rgb(40,90,130)"></i>low energy <i style="background:rgb(210,230,240)"></i>full`);
  if (mode === "generation") lines.push(`<i style="background:rgb(40,90,130)"></i>early generation <i style="background:rgb(210,230,240)"></i>latest`);
  lines.push(`<i style="background:#6bd39e"></i>food biomass`);
  if (app.world?.predatorsEnabled) lines.push(`<i style="background:${PREDATOR}"></i>predators`);
  if (app.world?.hazards.length) lines.push(`<i style="background:rgba(238,110,107,0.4)"></i>hazard`);
  $("legend").innerHTML = lines.map((l) => `<span>${l}</span>`).join("");
}

// ------------------------------------------------------------------ config panel
function setupConfigPanel() {
  const text = $("cfg-text") as HTMLTextAreaElement;
  const msg = $("cfg-msg");
  const show = (s: string, ok: boolean) => { msg.textContent = s; msg.className = `msg ${ok ? "ok" : "err"}`; };
  const parse = (): string | null => {
    try {
      JSON.parse(text.value);
      return text.value;
    } catch (e) {
      show(`Not valid JSON: ${(e as Error).message}`, false);
      return null;
    }
  };
  $("cfg-validate").addEventListener("click", () => {
    const t = parse();
    if (!t) return;
    const err = app.engine.validate(t);
    show(err ?? "Configuration is valid.", !err);
  });
  $("cfg-apply").addEventListener("click", () => {
    const t = parse();
    if (!t) return;
    const err = app.engine.validate(t);
    if (err) { show(err, false); return; }
    setRunning(false);
    $input("seed").value = String(JSON.parse(t).seed ?? 42);
    app.checkpoints = [];
    startFromConfig(t);
    show("Applied. The simulation restarted from tick 0.", true);
  });
  $("cfg-download").addEventListener("click", () =>
    download(`evolab-config-seed${app.world?.seed ?? 0}-tick${app.engine.tick()}.json`, app.engine.config(), "application/json"));
  $input("cfg-file").addEventListener("change", async (e) => {
    const f = (e.target as HTMLInputElement).files?.[0];
    if (!f) return;
    text.value = await f.text();
    (e.target as HTMLInputElement).value = "";
    const err = app.engine.validate(text.value);
    show(err ? `Loaded ${f.name}, but it is not valid:\n${err}` : `Loaded ${f.name}. Press "Apply and restart" to use it.`, !err);
  });
  $("ckpt-save").addEventListener("click", () =>
    download(`evolab-tick${app.engine.tick()}.evockpt`, app.engine.saveCheckpoint()));
  $input("ckpt-file").addEventListener("change", async (e) => {
    const f = (e.target as HTMLInputElement).files?.[0];
    (e.target as HTMLInputElement).value = "";
    if (!f) return;
    try {
      setRunning(false);
      app.engine.loadCheckpoint(new Uint8Array(await f.arrayBuffer()));
      app.checkpoints = [];
      onNewState(true);
      show(`Resumed ${f.name} at tick ${app.engine.tick()}.`, true);
    } catch (err) {
      show(`Could not load ${f.name}: ${(err as Error).message}`, false);
    }
  });
  $select("ckpt-every").addEventListener("change", (e) => {
    app.checkpointEvery = Number((e.target as HTMLSelectElement).value);
  });
  $("gene-table").innerHTML = `<tr><th>Gene</th><th>Min</th><th>Max</th><th>Effect</th></tr>` +
    app.genes.map((g) => `<tr><td>${escapeHtml(geneLabel(g.name))}</td><td>${g.min}</td><td>${g.max}</td><td>${escapeHtml(g.description)}</td></tr>`).join("");
  $("engine-version").textContent = `Engine ${app.engine.version()}, compiled from C++ to WebAssembly. Identical config and seed reproduce the same run here and in the native evosim CLI.`;
}

boot().catch((err) => {
  console.error(err);
  setBanner(`EvoLab could not start: ${err instanceof Error ? err.message : String(err)}`, true);
});
