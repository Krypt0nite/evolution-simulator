#!/usr/bin/env python3
"""Audits the shipped experiment results against their specs, raw data and the claims in the docs.

For every experiment in results/:
  1. The resolved spec (results/<e>/spec.json) matches config/experiments/<e>.json
     (seeds, ticks, sampling, conditions and overrides) and its base config contains
     every value set in the base preset.
  2. runs.csv has exactly one row per (condition, seed) and every run lasted the spec's
     ticks unless it went extinct.
  3. Every statistic in summary.json (n, mean, SD, median, 95% t-interval, Cohen's d,
     Welch p, Wilson interval) is recomputed from runs.csv with numpy/scipy, an
     implementation independent of the engine's C++ statistics.
  4. Every number in the tables of docs/EXPERIMENTS.md, and the specific claims in its
     prose, is recomputed from runs.csv / timeseries.csv.
  5. Optionally (--repro DIR), runs.csv from a fresh re-run is compared cell by cell with
     the committed one (ms_per_tick excluded: it is wall-clock time).

Usage: python3 scripts/audit_results.py [--repro build/repro] [--out docs/RESULTS_AUDIT.md]
Needs numpy and scipy. Exit status is non-zero if anything fails.
"""
import argparse
import csv
import json
import math
import re
import sys
import warnings
from pathlib import Path

import numpy as np
from scipy import stats

ROOT = Path(__file__).resolve().parent.parent
sys.stdout.reconfigure(encoding="utf-8")  # Windows consoles default to a legacy code page
warnings.filterwarnings("ignore", message="Precision loss")  # scipy, on nearly constant metrics
ap = argparse.ArgumentParser()
ap.add_argument("--repro", help="directory with re-run outputs (<experiment>/runs.csv)")
ap.add_argument("--out", help="write a Markdown report here")
args = ap.parse_args()

report: list[str] = []
failures = 0
checks = 0


def out(line=""):
    print(line)
    report.append(line)


def check(ok, what):
    global failures, checks
    checks += 1
    if not ok:
        failures += 1
    return ok


def fnum(s):
    try:
        v = float(s)
        return v if math.isfinite(v) else None
    except (TypeError, ValueError):
        return None


def load_runs(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def get_path(d, dotted):
    for k in dotted.split("."):
        if not isinstance(d, dict) or k not in d:
            return None
        d = d[k]
    return d


def subset_mismatches(small, big, prefix=""):
    """Keys set in `small` whose value differs in `big` (the resolved config)."""
    bad = []
    for k, v in small.items():
        p = f"{prefix}{k}"
        if isinstance(v, dict):
            bad += subset_mismatches(v, big.get(k, {}) if isinstance(big, dict) else {}, p + ".")
        elif not isinstance(big, dict) or big.get(k) != v:
            bad.append(p)
    return bad


# ------------------------------------------------------------------ statistics (independent)
def describe(v):
    a = np.asarray(v, dtype=float)
    n = len(a)
    m = a.mean() if n else math.nan
    sd = a.std(ddof=1) if n >= 2 else math.nan
    med = float(np.median(a)) if n else math.nan
    if n >= 2:
        h = stats.t.ppf(0.975, n - 1) * sd / math.sqrt(n)
        lo, hi = m - h, m + h
    else:
        lo = hi = math.nan
    return n, m, sd, med, lo, hi


def cohens_d(a, b):
    a, b = np.asarray(a, float), np.asarray(b, float)
    if len(a) < 2 or len(b) < 2:
        return math.nan
    pooled = math.sqrt(((len(a) - 1) * a.var(ddof=1) + (len(b) - 1) * b.var(ddof=1)) / (len(a) + len(b) - 2))
    if pooled <= 1e-300:
        return 0.0 if b.mean() == a.mean() else math.inf
    return (b.mean() - a.mean()) / pooled


def welch_p(a, b):
    a, b = np.asarray(a, float), np.asarray(b, float)
    if len(a) < 2 or len(b) < 2:
        return math.nan
    if a.var(ddof=1) / len(a) + b.var(ddof=1) / len(b) <= 1e-300:
        return 1.0 if a.mean() == b.mean() else 0.0
    return float(stats.ttest_ind(a, b, equal_var=False).pvalue)


def wilson(k, n, z=1.959964):
    p = k / n
    den = 1 + z * z / n
    c = (p + z * z / (2 * n)) / den
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / den
    return max(0.0, c - h), min(1.0, c + h)


def close(a, b, rel, abs_=1e-12):
    if a is None or b is None:
        return a is None and b is None
    if not math.isfinite(a) or not math.isfinite(b):
        return (math.isnan(a) and math.isnan(b)) or a == b
    return abs(a - b) <= max(abs_, rel * max(abs(a), abs(b)))


# ------------------------------------------------------------------ doc table parsing
ROW_METRICS = {
    "extinct": ["extinct"], "prey extinct": ["extinct"], "predators extinct": ["predators_extinct"],
    "mean population": ["mean_population"], "genetic diversity": ["mean_diversity", "final_diversity"],
    "share that reproduced": ["reproductive_success"], "final speed": ["final_speed"],
    "final body size": ["final_size"], "plant growth assimilated": ["energy_capture"],
    "mean lifespan": ["mean_lifespan"], "mean prey lifespan": ["mean_lifespan"],
    "max generation": ["max_generation"], "final maturity": ["final_maturity"],
    "final vision": ["final_vision"], "final sociality": ["final_social"],
    "final risk tolerance": ["final_riskTolerance"], "final metabolism": ["final_metabolism"],
    "final thermal optimum": ["final_thermalOptimum"],
}
NUM = r"[+\-−]?\d[\d,]*(?:\.\d+)?"


def to_float(s):
    return float(s.replace("−", "-").replace(",", "").replace("+", ""))


def decimals(s):
    return len(s.split(".")[1]) if "." in s else 0


def parse_cell(cell):
    c = cell.replace("**", "").strip()
    if c in ("n/a", ""):
        return None
    m = re.fullmatch(r"(\d+)/(\d+)", c)
    if m:
        return {"count": int(m.group(1)), "of": int(m.group(2))}
    m = re.match(rf"({NUM})(?:\s*±\s*({NUM}))?\s*(?:\((.*)\))?$", c)
    if not m:
        return {"unparsed": c}
    r = {"mean": m.group(1)}
    if m.group(2):
        r["sd"] = m.group(2)
    if m.group(3):
        dm = re.search(rf"d\s*=\s*({NUM})", m.group(3))
        pm = re.search(rf"p\s*([=<])\s*({NUM})", m.group(3))
        if dm:
            r["d"] = dm.group(1)
        if pm:
            r["p"] = (pm.group(1), pm.group(2))
    return r


def doc_tables():
    """{experiment number: [(header cells, [(row label, cells)])]} from docs/EXPERIMENTS.md."""
    text = (ROOT / "docs" / "EXPERIMENTS.md").read_text(encoding="utf-8")
    tables, current = {}, None
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        h = re.match(r"## E(\d)", lines[i])
        if h:
            current = int(h.group(1))
        if current and lines[i].startswith("|") and i + 1 < len(lines) and re.match(r"\|[-| ]+\|$", lines[i + 1]):
            header = [c.strip() for c in lines[i].strip("|").split("|")][1:]
            rows = []
            i += 2
            while i < len(lines) and lines[i].startswith("|"):
                cells = [c.strip() for c in lines[i].strip("|").split("|")]
                rows.append((cells[0], cells[1:]))
                i += 1
            tables.setdefault(current, []).append((header, rows))
            continue
        i += 1
    return tables


def within(value, shown, extra=1e-9):
    """True if `value` rounds to the displayed string `shown` (half an ulp of its precision)."""
    target = to_float(shown)
    tol = 0.5 * 10 ** (-decimals(shown)) + extra + 1e-9 * abs(target)
    return abs(value - target) <= tol


# ------------------------------------------------------------------ audit
experiments = sorted(p.name for p in (ROOT / "results").iterdir() if p.is_dir())
tables = doc_tables()
out("# Results audit")
out()
out("Generated by `scripts/audit_results.py`. Every check recomputes values from the committed raw data "
    "(`results/<experiment>/runs.csv`, `timeseries.csv`) with numpy/scipy, independently of the engine's C++ "
    "statistics code.")
out()

claims_ok = claims_bad = 0
for exp in experiments:
    d = ROOT / "results" / exp
    src = json.loads((ROOT / "config" / "experiments" / f"{exp}.json").read_text())
    spec = json.loads((d / "spec.json").read_text())
    summary = json.loads((d / "summary.json").read_text())
    runs = load_runs(d / "runs.csv")
    conds = [c["name"] for c in spec["conditions"]]
    out(f"## {exp}")
    out()

    # 1. spec
    seeds_expected = list(range(src.get("seedStart", 1000), src.get("seedStart", 1000) + src["seeds"]))
    problems = []
    if spec["seeds"] != seeds_expected:
        problems.append(f"seeds {spec['seeds']} != {seeds_expected}")
    for k in ("ticks", "sampleInterval", "name"):
        if spec.get(k) != src.get(k):
            problems.append(f"{k}: {spec.get(k)} != {src.get(k)}")
    if spec.get("recoveryAfterTick", -1) != src.get("recoveryAfterTick", -1):
        problems.append("recoveryAfterTick differs")
    if [(c["name"], c.get("overrides", {})) for c in spec["conditions"]] != [(c["name"], c.get("overrides", {})) for c in src["conditions"]]:
        problems.append("conditions/overrides differ")
    base_src = json.loads((ROOT / "config" / Path(src["baseConfig"]).name).read_text())
    bad = subset_mismatches(base_src, spec["base"])
    if bad:
        problems.append(f"base config differs at {bad[:5]}")
    check(not problems, "spec")
    out(f"- **Spec:** {'OK' if not problems else 'MISMATCH: ' + '; '.join(problems)}. "
        f"{len(spec['seeds'])} seeds ({spec['seeds'][0]}–{spec['seeds'][-1]}), {spec['ticks']:,} ticks, "
        f"sample every {spec['sampleInterval']} ticks, base `{src['baseConfig']}`.")
    for c in spec["conditions"]:
        ov = "; ".join(f"`{k}` = `{json.dumps(v)}`" for k, v in c.get("overrides", {}).items()) or "(base configuration)"
        out(f"  - `{c['name']}`: {ov}")

    # 2. runs.csv coverage
    per = {c: sorted(int(r["seed"]) for r in runs if r["condition"] == c) for c in conds}
    cover_ok = all(per[c] == spec["seeds"] for c in conds) and len(runs) == len(conds) * len(spec["seeds"])
    short = [r for r in runs if int(r["ticks_run"]) != spec["ticks"] and r["extinct"] != "1"]
    check(cover_ok and not short, "runs")
    out(f"- **Raw runs:** {len(runs)} rows = {len(conds)} conditions × {len(spec['seeds'])} seeds: "
        f"{'OK' if cover_ok else 'MISMATCH'}; runs ending early without extinction: {len(short)}.")

    # 3. summary statistics
    metric_names = list(summary["conditions"][0]["metrics"].keys())
    values = {c: {m: [v for v in (fnum(r.get(m)) for r in runs if r["condition"] == c) if v is not None]
                  for m in metric_names} for c in conds}
    worst = {"mean/sd/median": 0, "ci": 0, "d": 0, "p": 0}
    stat_bad = []
    for ci, c in enumerate(conds):
        sc = summary["conditions"][ci]
        k = sum(1 for r in runs if r["condition"] == c and r["extinct"] == "1")
        lo, hi = wilson(k, len(per[c]))
        if sc["extinctions"] != k or not close(sc["extinctionCI"][0], lo, 1e-6) or not close(sc["extinctionCI"][1], hi, 1e-6, 1e-9):
            stat_bad.append(f"{c}: extinction count/Wilson interval")
        for m in metric_names:
            if m == "ms_per_tick":
                continue
            sm = sc["metrics"][m]
            n, mean, sd, med, clo, chi = describe(values[c][m])
            g = lambda key: sm.get(key) if sm.get(key) is not None else math.nan
            if sm["n"] != n:
                stat_bad.append(f"{c}/{m}: n {sm['n']} != {n}")
                continue
            for key, val in (("mean", mean), ("sd", sd), ("median", med)):
                if not close(g(key), val, 1e-9, 1e-12):
                    stat_bad.append(f"{c}/{m}: {key} {g(key)} != {val}")
            # The engine uses a t table rounded to 3 decimals: allow 0.05% of the half-width.
            if n >= 2 and math.isfinite(sd) and sd > 0:
                half = chi - mean
                err = max(abs(g("ciLo") - clo), abs(g("ciHi") - chi)) / half
                worst["ci"] = max(worst["ci"], err)
                if err > 5e-4:
                    stat_bad.append(f"{c}/{m}: CI [{g('ciLo')}, {g('ciHi')}] vs [{clo}, {chi}]")
            if ci > 0:
                dd, pp = cohens_d(values[conds[0]][m], values[c][m]), welch_p(values[conds[0]][m], values[c][m])
                if math.isfinite(dd) and math.isfinite(g("dVsBaseline")):
                    worst["d"] = max(worst["d"], abs(dd - g("dVsBaseline")))
                # JSON cannot hold ±infinity (d of two constant groups); the engine writes null.
                if sm.get("dVsBaseline") is None and not math.isfinite(dd):
                    pass
                elif not close(g("dVsBaseline"), dd, 1e-9, 1e-12):
                    stat_bad.append(f"{c}/{m}: d {g('dVsBaseline')} != {dd}")
                if math.isfinite(pp) and math.isfinite(g("pVsBaseline")):
                    worst["p"] = max(worst["p"], abs(pp - g("pVsBaseline")))
                if not close(g("pVsBaseline"), pp, 1e-6, 1e-10):
                    stat_bad.append(f"{c}/{m}: p {g('pVsBaseline')} != {pp}")
    check(not stat_bad, "summary")
    out(f"- **summary.json statistics:** {'OK' if not stat_bad else 'MISMATCH'} "
        f"({len(conds)} conditions × {len(metric_names) - 1} metrics; largest differences from scipy: "
        f"CI {worst['ci'] * 100:.3f}% of half-width, d {worst['d']:.1e}, p {worst['p']:.1e}).")
    for b in stat_bad[:10]:
        out(f"  - {b}")

    # 4. doc tables
    num = int(exp[1])
    for header, rows in tables.get(num, []):
        col_note = ", ".join(f"'{h}' → `{conds[i]}`" for i, h in enumerate(header) if i < len(conds))
        out(f"- **Table in EXPERIMENTS.md** (columns {col_note}):")
        for label, cells in rows:
            metrics = ROW_METRICS.get(label.lower())
            if not metrics:
                out(f"  - {label}: no metric mapping, not checked")
                continue
            for i, cell in enumerate(cells):
                if i >= len(conds):
                    break
                cl = parse_cell(cell)
                if cl is None or "unparsed" in (cl or {}):
                    if cl and "unparsed" in cl:
                        out(f"  - {label} / {conds[i]}: could not parse `{cell}`")
                    continue
                c = conds[i]
                msgs, ok_any, used = [], False, None
                for m in metrics:
                    v = values[c][m]
                    msgs = []
                    if "count" in cl:
                        cnt = int(sum(1 for x in v if x == 1))
                        ok = cnt == cl["count"] and len(v) == cl["of"]
                        if not ok:
                            msgs.append(f"count {cnt}/{len(v)}")
                    else:
                        n, mean, sd, *_ = describe(v)
                        ok = within(mean, cl["mean"])
                        if not ok:
                            msgs.append(f"mean {mean:.4g}")
                        if "sd" in cl and not within(sd, cl["sd"]):
                            ok = False
                            msgs.append(f"SD {sd:.4g}")
                        if "d" in cl:
                            dd = cohens_d(values[conds[0]][m], v)
                            if not within(dd, cl["d"]):
                                ok = False
                                msgs.append(f"d {dd:.3g}")
                        if "p" in cl:
                            pp = welch_p(values[conds[0]][m], v)
                            op, pv = cl["p"]
                            pok = pp < to_float(pv) if op == "<" else within(pp, pv)
                            if not pok:
                                ok = False
                                msgs.append(f"p {pp:.3g}")
                    if ok:
                        ok_any, used = True, m
                        break
                check(ok_any, "claim")
                if ok_any:
                    claims_ok += 1
                else:
                    claims_bad += 1
                    out(f"  - **MISMATCH** {label} / {c}: doc `{cell}`, data: {', '.join(msgs)}")
                if ok_any and len(metrics) > 1:
                    out(f"  - {label}: matches `{used}`")
        out(f"  - all other cells match the data to the precision shown")

    # 5. reproduction
    if args.repro:
        rp = Path(args.repro) / exp / "runs.csv"
        if rp.exists():
            new = load_runs(rp)
            cols = [k for k in runs[0].keys() if k != "ms_per_tick"]
            key = lambda r: (r["condition"], r["seed"])
            a = {key(r): r for r in runs}
            b = {key(r): r for r in new}
            diff = [f"{k}/{col}" for k in a for col in cols if k not in b or a[k][col] != b[k][col]]
            check(not diff and len(a) == len(b), "repro")
            out(f"- **Re-run reproduces runs.csv:** {'yes, every cell identical' if not diff else 'NO: ' + ', '.join(diff[:6])} "
                f"({len(a)} runs × {len(cols)} columns; ms_per_tick excluded).")
            tp = Path(args.repro) / exp / "timeseries.csv"
            if tp.exists():
                same = (d / "timeseries.csv").read_text().splitlines() == tp.read_text().splitlines()
                check(same, "repro-ts")
                out(f"- **Re-run reproduces timeseries.csv:** {'yes, identical' if same else 'NO'}.")
        else:
            out(f"- **Re-run:** no {rp}")
    out()

# ------------------------------------------------------------------ prose claims
out("## Specific claims in the prose")
out()


def runs_of(exp):
    return load_runs(ROOT / "results" / exp / "runs.csv")


def col(rows, cond, name):
    return [fnum(r[name]) for r in rows if r["condition"] == cond]


def claim(text, ok, detail):
    global claims_ok, claims_bad
    check(ok, "prose")
    if ok:
        claims_ok += 1
    else:
        claims_bad += 1
    out(f"- {'OK' if ok else '**NOT SUPPORTED**'}: {text} ({detail})")


r1 = runs_of("e1_mutation_rate")
a, b = np.mean(col(r1, "low_0.005", "reproductive_success")), np.mean(col(r1, "high_0.25", "reproductive_success"))
claim("E1: share that reproduced fell from 53% to 47%", round(a * 100) == 53 and round(b * 100) == 47, f"{a:.3f} → {b:.3f}")

with open(ROOT / "results/e1_mutation_rate/timeseries.csv", newline="") as f:
    ts1 = [r for r in csv.DictReader(f) if r["condition"] == "low_0.005"]
at = lambda t, g: float(np.mean([float(r["mean_" + g]) for r in ts1 if int(r["tick"]) == t]))
claim("E1: at the low rate speed still rose from 1.20 (tick 0) to 1.88 and vision fell from 17.5 to 11.5",
      [round(at(0, "speed"), 2), round(at(15000, "speed"), 2), round(at(0, "vision"), 1), round(at(15000, "vision"), 1)] == [1.20, 1.88, 17.5, 11.5],
      f"speed {at(0, 'speed'):.3f} → {at(15000, 'speed'):.3f}, vision {at(0, 'vision'):.2f} → {at(15000, 'vision'):.2f}")

r2 = runs_of("e2_reproductive_investment")
lp = welch_p(col(r2, "early_fixed", "mean_lifespan"), col(r2, "late_fixed", "mean_lifespan"))
claim("E2: late maturers lived somewhat longer (p = 0.06)", round(lp, 2) == 0.06, f"Welch p = {lp:.3f}")
r7 = runs_of("e7_behavioral_strategies")
a7, b7 = col(r7, "utility_evolving", "mean_population"), col(r7, "utility_frozen_weights", "mean_population")
claim("E7: frozen weights 543 vs 484, p = 0.12", (round(np.mean(b7)), round(np.mean(a7)), round(welch_p(a7, b7), 2)) == (543, 484, 0.12),
      f"{np.mean(b7):.1f} vs {np.mean(a7):.1f}, p = {welch_p(a7, b7):.3f}")
conds2 = [c["name"] for c in json.loads((ROOT / "results/e2_reproductive_investment/spec.json").read_text())["conditions"]]
mixed = sorted(col(r2, conds2[2], "final_maturity"))
early = [x for x in mixed if x < 150]
late = [x for x in mixed if x >= 150]
claim("E2: mixed populations ended near the early strategy in 8/10 runs (29–92 ticks), near the late one in 2/10 (200 and 282)",
      len(early) == 8 and round(min(early)) == 29 and round(max(early)) == 92 and [round(x) for x in late] == [200, 282],
      f"early-like {len(early)}: {min(early):.1f}–{max(early):.1f}; late-like {[round(x, 1) for x in late]}")

r4 = runs_of("e4_predator_pressure")
spec4 = json.loads((ROOT / "results/e4_predator_pressure/spec.json").read_text())
c4 = [c["name"] for c in spec4["conditions"]]
claim("E4: coevolving predators went extinct in 10/10 runs", sum(col(r4, c4[1], "predators_extinct")) == 10,
      f"{int(sum(col(r4, c4[1], 'predators_extinct')))}/10")
intro = get_path(spec4["base"], "predators.introduceAtTick")
with open(ROOT / "results/e4_predator_pressure/timeseries.csv", newline="") as f:
    ts = [r for r in csv.DictReader(f) if r["condition"] == c4[1]]
last_alive, first_gone = [], []
for seed in spec4["seeds"]:
    rows = sorted((int(r["tick"]), int(float(r["predators"]))) for r in ts if int(r["seed"]) == seed)
    t0 = next((t for t, p in rows if t > intro and p == 0), None)
    if t0 is not None:
        first_gone.append(t0 - intro)
        last_alive.append(max(t for t, p in rows if t < t0 and p > 0) - intro)
claim(f"E4: predators (introduced at tick {intro}) went extinct after 1,750–2,000 ticks at the earliest and "
      "6,750–7,000 at the latest (250-tick samples)",
      len(first_gone) == 10 and (min(last_alive), min(first_gone), max(last_alive), max(first_gone)) == (1750, 2000, 6750, 7000),
      f"last sample with predators +{min(last_alive)}…+{max(last_alive)}, first without +{min(first_gone)}…+{max(first_gone)}")
pers = [fp for fp, pe in zip(col(r4, c4[2], "final_predators"), col(r4, c4[2], "predators_extinct")) if pe == 0]
claim("E4: with prey speed fixed, predators persisted in 4/10 runs, ending with 47–155 predators",
      len(pers) == 4 and min(pers) == 47 and max(pers) == 155, f"{len(pers)} runs, final predators {sorted(int(p) for p in pers)}")
claim("E4: with prey speed fixed, prey went extinct once", sum(col(r4, c4[2], "extinct")) == 1, f"{int(sum(col(r4, c4[2], 'extinct')))}/10")
m = [np.mean([x for x in col(r4, c, "final_maturity") if x is not None]) for c in c4]
claim("E4: final maturity 153 → 111 → 100 ticks", [round(x) for x in m] == [153, 111, 100], " → ".join(f"{x:.1f}" for x in m))

r6 = runs_of("e6_environmental_change")
spec6 = json.loads((ROOT / "results/e6_environmental_change/spec.json").read_text())
c6 = [c["name"] for c in spec6["conditions"]]
rec = col(r6, c6[1], "recovery_ticks")
fin6 = [x for x in rec if x is not None]
claim("E6: after the shift, 8/10 populations recovered to 90% of their pre-shift size, median ≈1,600 ticks",
      len(fin6) == 8 and round(float(np.median(fin6)), -2) == 1600, f"{len(fin6)}/10 recovered, median {np.median(fin6):.0f} ticks")

# The experiment descriptions in the docs against the specs' actual overrides.
def ov(exp, cond):
    s = json.loads((ROOT / "results" / exp / "spec.json").read_text())
    return next(c["overrides"] for c in s["conditions"] if c["name"] == cond), s["base"]


DESCRIPTIONS = [
    ("E1: fixed per-gene mutation probability 0.005 / 0.05 / 0.25",
     lambda: [ov("e1_mutation_rate", c)[0] for c in ("low_0.005", "medium_0.05", "high_0.25")]
     == [{"mutation.mode": "fixed", "mutation.fixedRate": r} for r in (0.005, 0.05, 0.25)]),
    ("E2: maturity locked at 30–40 vs 250–300 ticks, plus a 50/50 mixed population free to evolve",
     lambda: ov("e2_reproductive_investment", "early_fixed")[0]["population.initialTraits"] == {"maturity": [30, 40]}
     and ov("e2_reproductive_investment", "late_fixed")[0]["population.initialTraits"] == {"maturity": [250, 300]}
     and "population.lockedGenes" not in ov("e2_reproductive_investment", "mixed_evolving")[0]
     and [g["fraction"] for g in ov("e2_reproductive_investment", "mixed_evolving")[0]["population.groups"]] == [0.5, 0.5]),
    ("E3: uniform capacity 4.0 per cell = 38,400 units in total; patches relocate every 1,500 ticks",
     lambda: (lambda o, b: o["resources.capacity"] == 4 and
              b["world"]["width"] * b["world"]["height"] / b["resources"]["cellSize"] ** 2 * 4 == 38400)(*ov("e3_resource_distribution", "uniform"))
     and ov("e3_resource_distribution", "shifting")[0]["resources.shiftPeriod"] == 1500),
    ("E4: 40 predators introduced at tick 1,500; prey speed locked in the third condition",
     lambda: (lambda b: b["predators"]["initialCount"] == 40 and b["predators"]["introduceAtTick"] == 1500)(ov("e4_predator_pressure", "predators_coevolving")[1])
     and ov("e4_predator_pressure", "predators_prey_speed_fixed")[0]["population.lockedGenes"] == ["speed"]),
    ("E5: vision locked at 6, 20 or 45",
     lambda: [ov("e5_vision_vs_metabolism", f"vision_{v}")[0]["population.initialTraits"]["vision"] for v in (6, 20, 45)]
     == [[6, 6], [20, 20], [45, 45]]),
    ("E6: +1.0 temperature shift at tick 6,000; oscillation amplitude 0.8, period 1,500",
     lambda: ov("e6_environmental_change", "abrupt_shift")[0]["environment.events"] == [{"tick": 6000, "type": "temperatureShift", "value": 1}]
     and ov("e6_environmental_change", "oscillating")[0] == {"environment.cycleAmplitude": 0.8, "environment.cyclePeriod": 1500}),
    ("E7: behaviour genes frozen (wForage, wExplore, wRest, wFlee, wMate, social, riskTolerance); neural controller",
     lambda: ov("e7_behavioral_strategies", "utility_frozen_weights")[0]["population.lockedGenes"]
     == ["wForage", "wExplore", "wRest", "wFlee", "wMate", "social", "riskTolerance"]
     and ov("e7_behavioral_strategies", "neural")[0] == {"controller.type": "neural"}),
]
for text, fn in DESCRIPTIONS:
    claim(text, bool(fn()), "condition overrides in spec.json")
out("- Not independently checked: the ≈35,100 units of capacity quoted for the default patches in E3 "
    "(it depends on the engine's patch placement for seed 1000).")

# Power statement in the Method section.
pw = 1 - stats.nct.cdf(stats.t.ppf(0.975, 18), 18, 1.3 * math.sqrt(5)) + stats.nct.cdf(-stats.t.ppf(0.975, 18), 18, 1.3 * math.sqrt(5))
claim("Method: 10 seeds give reasonable power for |d| ≳ 1.3 at α = 0.05", pw >= 0.75,
      f"power of a two-sided two-sample t-test with n = 10 per group at d = 1.3: {pw:.2f}")
out()
out(f"**Summary:** {checks} checks, {failures} failed; {claims_ok} documented numbers/claims confirmed, {claims_bad} not supported.")

if args.out:
    Path(args.out).write_text("\n".join(report) + "\n", encoding="utf-8")
sys.exit(1 if failures else 0)
