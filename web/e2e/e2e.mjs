// End-to-end test of the production build in a real browser (local Chrome or Edge
// via puppeteer-core). Serves web/dist with `vite preview`, drives the UI, and checks
// state hashes against an independent copy of the same WebAssembly engine run in Node.
//
// Usage (from web/): npm run build && npm run test:e2e
//        node e2e/e2e.mjs [--url https://deployed.example/]   (test a deployed site instead)
import path from "node:path";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import { fileURLToPath, pathToFileURL } from "node:url";
import puppeteer from "puppeteer-core";
import { preview } from "vite";
import { chromePath } from "./chrome.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const dist = path.resolve(here, "../dist");
const urlArg = process.argv.indexOf("--url");

// ------------------------------------------------------------------ reference engine (Node)
globalThis.require = createRequire(import.meta.url);  // Emscripten's ES6 glue under Node
globalThis.__dirname = path.join(dist, "wasm");
const wasmJs = path.join(dist, "wasm", "evosim.mjs");
const { default: createEvoModule } = await import(pathToFileURL(wasmJs).href);
const M = await createEvoModule({ wasmBinary: readFileSync(path.join(dist, "wasm", "evosim.wasm")) });
function referenceHash(configJson, tick) {
  const n = M.lengthBytesUTF8(configJson) + 1, p = M._malloc(n);
  M.stringToUTF8(configJson, p, n);
  const ok = M._evo_create(p);
  M._free(p);
  if (ok !== 1) throw new Error(`reference create failed: ${M.UTF8ToString(M._evo_last_error())}`);
  while (M._evo_tick() < tick) M._evo_step(1);
  return M.UTF8ToString(M._evo_hash());
}

// ------------------------------------------------------------------ harness
let server = null, base;
if (urlArg > 0) base = process.argv[urlArg + 1];
else {
  server = await preview({ root: path.resolve(here, ".."), preview: { port: 4179, strictPort: true }, logLevel: "silent" });
  base = "http://localhost:4179/";
}
const browser = await puppeteer.launch({ executablePath: chromePath(), headless: true, args: ["--no-sandbox"] });
const problems = [];
const results = [];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function newPage(query = "?intro=0&test=1", viewport = { width: 1440, height: 900 }) {
  const ctx = await browser.createBrowserContext();  // fresh storage per page
  const page = await ctx.newPage();
  await page.setViewport(viewport);
  page.on("console", (m) => { if (m.type() === "error") problems.push(`console error: ${m.text()}`); });
  page.on("pageerror", (e) => problems.push(`page error: ${e.message}`));
  page.on("requestfailed", (r) => {
    // Google Fonts may be unreachable offline; the app falls back to system fonts.
    if (!r.url().includes("fonts.g")) problems.push(`request failed: ${r.url()} ${r.failure()?.errorText}`);
  });
  page.on("response", (r) => { if (r.status() >= 400) problems.push(`HTTP ${r.status()}: ${r.url()}`); });
  await page.goto(base + query, { waitUntil: "networkidle0" });
  await page.waitForFunction(() => window.__evolab ? true : !document.getElementById("intro").hidden, { timeout: 30000 });
  return page;
}
const E = (page, expr) => page.evaluate(expr);
const tick = (page) => E(page, () => window.__evolab.tick());
async function test(name, fn) {
  const t0 = Date.now();
  try {
    await fn();
    results.push([true, name, Date.now() - t0]);
    console.log(`  ok    ${name} (${Date.now() - t0} ms)`);
  } catch (e) {
    results.push([false, name, Date.now() - t0]);
    console.log(`  FAIL  ${name}: ${e.message}`);
  }
}
function assert(cond, msg) { if (!cond) throw new Error(msg); }
async function runUntil(page, minTick, timeout = 60000) {
  await page.waitForFunction((t) => window.__evolab.tick() >= t, { timeout, polling: 50 }, minTick);
}
async function pause(page) {
  if (await E(page, () => window.__evolab.running())) await page.click("#btn-run");
  await sleep(50);
}

console.log(`EvoLab end-to-end tests against ${base}`);

// ------------------------------------------------------------------ tests
await test("first visit shows the intro; Start evolution runs the simulation", async () => {
  const page = await newPage("?test=1");
  assert(await E(page, () => !document.getElementById("intro").hidden), "intro not shown on first visit");
  assert(await tick(page) === 0, "simulation advanced before Start");
  await page.click("#intro-start");
  await runUntil(page, 20);
  assert(await E(page, () => document.getElementById("intro").hidden), "intro still visible");
  assert(await E(page, () => document.getElementById("btn-run").textContent === "Pause"), "Run button not showing Pause");
  // Second visit in the same profile: intro remembered as seen.
  await page.reload({ waitUntil: "networkidle0" });
  await page.waitForFunction(() => !!window.__evolab);
  assert(await E(page, () => document.getElementById("intro").hidden), "intro shown again after it was dismissed");
  assert(await E(page, () => !document.getElementById("paused-cta").hidden), "paused call-to-action missing");
  await page.close();
});

await test("autostart=1 starts without the intro", async () => {
  const page = await newPage("?test=1&autostart=1");
  assert(await E(page, () => document.getElementById("intro").hidden), "intro shown despite autostart");
  await runUntil(page, 10);
  await page.close();
});

const page = await newPage();
let cfg0;
await test("pause, resume, step and reset", async () => {
  cfg0 = await E(page, () => window.__evolab.config());
  const h0 = await E(page, () => window.__evolab.hash());
  await page.click("#cta-run");
  await runUntil(page, 50);
  await pause(page);
  const t1 = await tick(page);
  await sleep(400);
  assert(await tick(page) === t1, "tick advanced while paused");
  assert(await E(page, () => document.getElementById("btn-run").textContent === "Resume"), "button should read Resume");
  await page.click("#btn-step");
  assert(await tick(page) === t1 + 1, "Step did not advance exactly one tick");
  await page.click("#btn-run");
  await runUntil(page, t1 + 30);
  await pause(page);
  await page.click("#btn-reset");
  assert(await tick(page) === 0, "Reset did not return to tick 0");
  assert(await E(page, () => window.__evolab.hash()) === h0, "state after Reset differs from the initial state");
});

await test("UI trajectory matches an independent reference engine at the paused tick", async () => {
  await page.click("#btn-run");
  await runUntil(page, 300);
  await pause(page);
  const t = await tick(page), h = await E(page, () => window.__evolab.hash());
  assert(referenceHash(cfg0, t) === h, `hash mismatch at tick ${t}`);
});

await test("organism inspection by clicking the plate", async () => {
  const o = await E(page, () => window.__evolab.organism(0));
  const box = await (await page.$("#world")).boundingBox();
  await page.mouse.click(box.x + o.sx, box.y + o.sy);
  await sleep(100);
  const ok = await E(page, () => document.querySelector('.tabs [aria-selected="true"]').dataset.tab === "inspect"
    && !!document.querySelector("#inspector .insp-head"));
  assert(ok, "inspector did not open for the clicked organism");
  const text = await E(page, () => document.querySelector("#inspector .insp-head").textContent);
  assert(text.includes(String(o.id)), `inspector does not show organism #${o.id}: ${text.slice(0, 80)}`);
});

await test("environment changes are applied, logged and recorded in the config", async () => {
  await page.click('.tabs [data-tab="environment"]');
  await page.$eval("#env-food", (el) => { el.value = "0.5"; el.dispatchEvent(new Event("input")); });
  await page.click('[data-env="foodMultiplier"]');
  await page.$eval("#env-pred", (el) => { el.value = "15"; });
  await page.click('[data-env="introducePredators"]');
  const log = await E(page, () => document.getElementById("event-log").textContent);
  assert(log.includes("food capacity set to 0.50") && log.includes("15 predators introduced"), `event log: ${log}`);
  const cfg = JSON.parse(await E(page, () => window.__evolab.config()));
  assert(cfg.environment.events.length === 2, "events not recorded in the config");
  await page.click("#btn-run");
  await page.waitForFunction(() => window.__evolab.observations().some((o) => o.kind === "predators-in"), { timeout: 20000 });
  assert(await E(page, () => !!document.querySelector(".toast")), "no toast shown for predator introduction");
  await pause(page);
  // The recorded config replays the interactive session exactly.
  const t = await tick(page), h = await E(page, () => window.__evolab.hash());
  assert(referenceHash(await E(page, () => window.__evolab.config()), t) === h, "config with events does not replay the session");
});

await test("checkpoint restore rolls back to the exact saved state", async () => {
  await page.click('.tabs [data-tab="config"]');
  await page.select("#ckpt-every", "500");
  await page.click("#btn-reset");
  await page.select("#speed", "max");
  await page.click("#btn-run");
  await runUntil(page, 1300);
  await pause(page);
  const cps = await E(page, () => window.__evolab.checkpoints());
  assert(cps.includes(500) && cps.includes(1000), `checkpoints: ${cps}`);
  // Click the timeline near its left third, then restore the nearest checkpoint.
  const over = await (await page.$("#chart-timeline .u-over")).boundingBox();
  await page.mouse.move(over.x + over.width * 0.55, over.y + over.height / 2);
  await page.mouse.click(over.x + over.width * 0.55, over.y + over.height / 2);
  await page.waitForSelector("#tl-restore", { timeout: 5000 });
  const label = await E(page, () => document.getElementById("tl-restore").textContent);
  const target = Number(label.replace(/[^0-9]/g, ""));
  await page.click("#tl-restore");
  assert(await tick(page) === target, `restored tick ${await tick(page)} != ${target}`);
  const h = await E(page, () => window.__evolab.hash());
  assert(referenceHash(await E(page, () => window.__evolab.config()), target) === h, "restored state differs from the reference");
});

await test("experiment runs to completion in the worker", async () => {
  await page.click('.tabs [data-tab="experiments"]');
  await page.$eval("#exp-seeds", (el) => { el.value = "2"; });
  await page.$eval("#exp-ticks", (el) => { el.value = "300"; });
  await page.click("#exp-run");
  await page.waitForFunction(() => /Finished/.test(document.querySelector("#exp-progress span").textContent), { timeout: 120000 });
  const rows = await E(page, () => document.querySelectorAll("#exp-table tr").length);
  assert(rows > 3, "results table is empty");
});
await page.close();

await test("UI update frequency and speed do not change the trajectory or the population observations", async () => {
  const runs = [];
  for (const [ui, speed] of [["16", "5"], ["2000", "max"]]) {
    const p = await newPage(`?intro=0&test=1&ui=${ui}`);
    await p.select("#preset", "predator_prey");
    await p.select("#speed", speed);
    const cfg = await E(p, () => window.__evolab.config());
    await p.click("#btn-run");
    await runUntil(p, 2600, 180000);  // predators arrive at tick 1,500 in this preset
    await pause(p);
    const t = await tick(p), h = await E(p, () => window.__evolab.hash());
    assert(referenceHash(cfg, t) === h, `ui=${ui}: hash mismatch with reference at tick ${t}`);
    const obs = (await E(p, () => window.__evolab.observations())).filter((o) => o.kind !== "cluster");
    runs.push({ t, obs });
    await p.close();
  }
  // Observations up to the shorter run's last tick must be identical.
  const upTo = Math.min(runs[0].t, runs[1].t) - 50;
  const key = (r) => JSON.stringify(r.obs.filter((o) => o.tick <= upTo));
  assert(runs[0].obs.some((o) => o.kind === "predators-in" && o.tick <= upTo), "expected a predator-introduction observation");
  assert(key(runs[0]) === key(runs[1]), "population observations differ between UI cadences");
  console.log(`        compared ${runs[0].obs.filter((o) => o.tick <= upTo).length} observations through tick ${upTo}; paused at ticks ${runs[0].t} and ${runs[1].t}`);
});

for (const [w, h] of [[390, 844], [768, 1024], [1440, 900]]) {
  await test(`layout at ${w}px: no horizontal scroll, plate fully visible, legend and hint clear of the plate`, async () => {
    const p = await newPage("?intro=0&test=1", { width: w, height: h });
    const r = await E(p, () => {
      const box = (id) => document.getElementById(id).getBoundingClientRect();
      const c = box("world"), lg = box("legend");
      const hint = [...document.querySelectorAll(".stage-hint")].map((e) => e.getBoundingClientRect()).find((b) => b.width > 0);
      const overlap = (a, b) => a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
      return {
        scroll: document.documentElement.scrollWidth - document.documentElement.clientWidth,
        canvasRight: c.right, vw: document.documentElement.clientWidth, canvasH: c.height,
        legend: overlap(c, lg), hint: hint ? overlap(c, hint) : false,
      };
    });
    assert(r.scroll <= 0, `page scrolls horizontally by ${r.scroll}px`);
    assert(r.canvasRight <= r.vw + 0.5, `canvas extends past the viewport (${r.canvasRight} > ${r.vw})`);
    assert(r.canvasH >= 180, `plate too small (${r.canvasH}px)`);
    assert(!r.legend && !r.hint, "legend or hint overlaps the plate");
    // The whole world must be inside the canvas after fitting.
    const fit = await E(p, () => {
      const cfg = JSON.parse(window.__evolab.config());
      const c = document.getElementById("world").getBoundingClientRect();
      const o = window.__evolab.organism(0);
      return { ok: !!o && o.sx >= 0 && o.sy >= 0 && o.sx <= c.width && o.sy <= c.height, w: cfg.world.width };
    });
    assert(fit.ok, "organisms drawn outside the visible plate");
    if (w < 1000) {
      assert(await E(p, () => getComputedStyle(document.getElementById("controls-extra")).display === "none"), "extra controls not collapsed");
      await p.click("#btn-more");
      assert(await E(p, () => getComputedStyle(document.getElementById("controls-extra")).display !== "none"), "Settings did not reveal controls");
      assert(await E(p, () => document.getElementById("preset").getBoundingClientRect().width > 0), "World selector not reachable");
    }
    await p.close();
  });
}

await browser.close();
if (server) await new Promise((r) => server.httpServer.close(r));

const failed = results.filter((r) => !r[0]).length;
if (problems.length) {
  console.log(`\nConsole errors / failed requests (${problems.length}):`);
  for (const p of [...new Set(problems)]) console.log(`  ${p}`);
}
console.log(`\n${results.length - failed}/${results.length} tests passed; ${problems.length} console errors or failed requests.`);
process.exit(failed || problems.length ? 1 : 0);
