export function $(id: string): HTMLElement {
  const el = document.getElementById(id);
  if (!el) throw new Error(`missing element #${id}`);
  return el;
}

export function $input(id: string): HTMLInputElement { return $(id) as HTMLInputElement; }
export function $select(id: string): HTMLSelectElement { return $(id) as HTMLSelectElement; }

export function escapeHtml(s: string): string {
  return s.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]!);
}

export function download(name: string, data: BlobPart, type = "application/octet-stream") {
  const url = URL.createObjectURL(new Blob([data], { type }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/** Per-viewer UI preferences. Storage can be unavailable (private mode, blocked); then nothing is remembered. */
export function prefGet(key: string): string | null {
  try { return localStorage.getItem(`evolab.${key}`); } catch { return null; }
}
export function prefSet(key: string, value: string) {
  try { localStorage.setItem(`evolab.${key}`, value); } catch { /* not remembered */ }
}

export function readFile(input: HTMLInputElement): Promise<File | null> {
  return new Promise((resolve) => {
    const f = input.files?.[0] ?? null;
    input.value = "";
    resolve(f);
  });
}

/** Human labels for gene names used in the engine. */
export const GENE_LABELS: Record<string, string> = {
  speed: "Speed",
  vision: "Vision",
  metabolism: "Metabolism",
  size: "Body size",
  efficiency: "Efficiency",
  fertility: "Fertility",
  maturity: "Maturity age",
  lifespan: "Lifespan",
  riskTolerance: "Risk tolerance",
  social: "Sociality",
  mutationRate: "Mutation rate",
  investment: "Offspring investment",
  thermalOptimum: "Thermal optimum",
  wForage: "Forage weight",
  wExplore: "Explore weight",
  wRest: "Rest weight",
  wFlee: "Flee weight",
  wMate: "Mate weight",
};

export const geneLabel = (g: string) => GENE_LABELS[g] ?? g;
