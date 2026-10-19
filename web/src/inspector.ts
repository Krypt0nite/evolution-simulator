// Inspector panel: genome, phenotype, behaviour and family of one organism.
import type { GeneSpec, TraitMap } from "./engine";
import { fmt } from "./charts";
import { escapeHtml, geneLabel } from "./util";

export interface InspectorCallbacks {
  select(id: number): void;
  setFollow(on: boolean): void;
  setHighlight(founder: number): void;
}

export interface InspectorState {
  follow: boolean;
  highlightFounder: number;
}

const idLink = (id: number, alive = true) =>
  `<button class="link${alive ? "" : " gone"}" data-id="${id}" title="${alive ? "Inspect" : "Inspect (dead)"}">#${id}</button>`;

let lastHtml = "";
let delegated: HTMLElement | null = null;
let current: { o: any; state: InspectorState; cb: InspectorCallbacks } | null = null;

function installDelegation(el: HTMLElement) {
  if (delegated === el) return;
  delegated = el;
  el.addEventListener("click", (e) => {
    if (!current) return;
    const t = (e.target as HTMLElement).closest("button") as HTMLButtonElement | null;
    if (!t || !el.contains(t)) return;
    const { o, state, cb } = current;
    if (t.dataset.id) cb.select(Number(t.dataset.id));
    else if (t.id === "insp-follow") cb.setFollow(!state.follow);
    else if (t.id === "insp-lineage") cb.setHighlight(state.highlightFounder === o.founder ? 0 : o.founder);
  });
}

/** Writes html only when it changed, so buttons are not replaced under the cursor. */
function setHtml(el: HTMLElement, html: string) {
  if (html === lastHtml && el.innerHTML !== "") return;
  lastHtml = html;
  el.innerHTML = html;
}

export function renderInspector(
  el: HTMLElement,
  o: any,
  genes: GeneSpec[],
  popMeans: TraitMap | null,
  state: InspectorState,
  cb: InspectorCallbacks,
) {
  installDelegation(el);
  current = { o, state, cb };
  if (!o) {
    setHtml(el, `<p class="empty">This organism is not in the record (the lineage log may have reached its size limit).</p>`);
    return;
  }
  if (!o.alive) {
    setHtml(el, `
      <div class="insp-head"><h2>#${o.id}</h2><span class="tag dead">died at tick ${o.deathTick} (${escapeHtml(o.cause)})</span></div>
      <div class="row">
        <button id="insp-lineage" class="${state.highlightFounder === o.founder ? "active" : ""}">${state.highlightFounder === o.founder ? "Lineage highlighted" : "Highlight lineage"}</button>
      </div>
      <dl class="readout">
        <div><dt>Born</dt><dd>tick ${o.birthTick}</dd></div>
        <div><dt>Lived</dt><dd>${o.deathTick - o.birthTick} ticks</dd></div>
        <div><dt>Generation</dt><dd>${o.generation}</dd></div>
        <div><dt>Offspring</dt><dd>${o.offspring}</dd></div>
        <div><dt>Descendants</dt><dd>${o.descendants}</dd></div>
        <div><dt>Founder</dt><dd>${idLink(o.founder)}</dd></div>
      </dl>
      <h3>Parents</h3>
      <div class="idlist">${o.parentA ? idLink(o.parentA) : "<span class=note>Founder of the starting population</span>"}${o.parentB ? idLink(o.parentB) : ""}</div>`);
    return;
  }
  const energyPct = Math.max(0, Math.min(100, (100 * o.energy) / o.maxEnergy));
  const recent: string[] = o.recentActions;
  const geneRows = genes.map((g) => {
    const v = o.genome[g.name];
    const pct = ((v - g.min) / (g.max - g.min)) * 100;
    const mean = popMeans?.[g.name];
    const meanPct = mean === undefined ? null : ((mean - g.min) / (g.max - g.min)) * 100;
    return `<div class="gene-row" title="${escapeHtml(g.description)} (range ${g.min} to ${g.max})">
      <span>${geneLabel(g.name)}</span>
      <div class="gene-track"><div class="fill" style="width:${pct.toFixed(1)}%"></div>${
        meanPct === null ? "" : `<div class="mean" style="left:calc(${meanPct.toFixed(1)}% - 1px)" title="Population mean ${fmt(mean!)}"></div>`}</div>
      <span>${fmt(v)}</span></div>`;
  }).join("");
  const p = o.phenotype, per = o.perception;
  const seen: string[] = [];
  if (per.foodSeen) seen.push(`food ${fmt(per.foodDist, 1)} away`);
  if (per.threatSeen) seen.push(`<span style="color:var(--red)">predator ${fmt(per.threatDist, 1)} away</span>`);
  if (per.preySeen) seen.push(`prey ${fmt(per.preyDist, 1)} away`);
  if (per.mateSeen) seen.push("a ready mate");
  if (per.hazardSeen) seen.push("a hazard");
  seen.push(`${per.neighbours} of its kind`);

  setHtml(el, `
    <div class="insp-head">
      <h2>#${o.id}</h2>
      <span class="tag ${o.kind}">${o.kind}</span>
      <span class="tag">generation ${o.generation}</span>
      <span class="tag">cluster ${o.cluster < 0 ? "unassigned" : o.cluster}</span>
    </div>
    <div class="row">
      <button id="insp-follow" class="${state.follow ? "active" : ""}">${state.follow ? "Following" : "Follow"}</button>
      <button id="insp-lineage" class="${state.highlightFounder === o.founder ? "active" : ""}">${state.highlightFounder === o.founder ? "Lineage highlighted" : "Highlight lineage"}</button>
    </div>
    <dl class="readout">
      <div><dt>Age</dt><dd>${o.age}<small>/ ${fmt(p.lifespan, 0)}</small></dd></div>
      <div><dt>Mature</dt><dd>${o.mature ? "yes" : `at ${fmt(p.maturity, 0)}`}</dd></div>
      <div><dt>Offspring</dt><dd>${o.offspring}</dd></div>
      <div><dt>Descendants</dt><dd>${o.descendants}</dd></div>
      <div><dt>Lineage alive</dt><dd>${o.lineageAlive}</dd></div>
      <div><dt>Breed again in</dt><dd>${o.cooldown} ticks</dd></div>
    </dl>
    <h3>Energy ${fmt(o.energy, 1)} of ${fmt(o.maxEnergy, 1)}</h3>
    <div class="energy-bar"><div style="width:${energyPct.toFixed(1)}%"></div></div>
    <h3>Behaviour</h3>
    <p class="note">Now: <b style="color:var(--ink)">${escapeHtml(o.action)}</b>. Sees ${seen.join(", ")}. Hunger ${fmt(per.hunger)}, temperature mismatch ${fmt(per.tempMismatch)}.</p>
    <div class="chips">${recent.map((a, i) => `<span class="chip${i === recent.length - 1 ? " latest" : ""}">${escapeHtml(a)}</span>`).join("")}</div>
    <h3>Genome <span class="note" style="margin:0">bar = this organism, amber tick = population mean</span></h3>
    ${geneRows}
    <h3>Expressed phenotype</h3>
    <table class="table">
      <tr><td>Max speed</td><td>${fmt(p.maxSpeed)}</td><td>Vision</td><td>${fmt(p.vision, 1)}</td></tr>
      <tr><td>Bite per tick</td><td>${fmt(p.biteRate)}</td><td>Assimilation</td><td>${fmt(p.assimilation)}</td></tr>
      <tr><td>Maintenance per tick</td><td>${fmt(p.basalCost, 3)}</td><td>Move cost coeff.</td><td>${fmt(p.moveCoeff, 4)}</td></tr>
      <tr><td>Breeding threshold</td><td>${fmt(p.reproThreshold, 1)}</td><td>Investment</td><td>${fmt(p.investment)}</td></tr>
      <tr><td>Temperature here</td><td>${fmt(p.temperatureHere)}</td><td>Thermal cost coeff.</td><td>${fmt(p.thermalCoeff, 3)}</td></tr>
    </table>
    ${o.brainWeights > 0 ? `<p class="note">Neural controller with ${o.brainWeights} evolved weights.</p>` : ""}
    <h3>Family</h3>
    <p class="note">Parents</p>
    <div class="idlist">${o.parentA ? idLink(o.parentA) : "<span class=note>Founder of the starting population</span>"}${o.parentB ? idLink(o.parentB) : ""}</div>
    ${o.ancestors.length ? `<p class="note" style="margin-top:8px">Ancestors, nearest first</p><div class="idlist">${o.ancestors.map((a: number) => idLink(a)).join("")}</div>` : ""}
    <p class="note" style="margin-top:8px">Children${o.children.length >= 24 ? " (first 24)" : ""}</p>
    <div class="idlist">${o.children.length ? o.children.map((c: any) => idLink(c.id, c.alive)).join("") : "<span class=note>None yet</span>"}</div>
    <p class="note" style="margin-top:8px">Founder ${idLink(o.founder)}</p>`);
}
