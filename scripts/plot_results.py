#!/usr/bin/env python3
"""Plot experiment outputs written by `evosim experiment`.

For each results/<experiment>/ directory this writes figure.png with:
  * three trajectories (mean across seeds with a shaded 95% CI band), and
  * one per-run outcome (every seed as a dot, mean and 95% CI as a bar).

Usage: scripts/plot_results.py [results-dir ...]   (default: every dir in results/)
Requires matplotlib.
"""
import csv
import json
import math
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

COLORS = ["#1f9fb8", "#d9952b", "#3aa876", "#7b6fe0", "#d65552", "#d469a8"]

# experiment prefix -> (trajectory columns, per-run outcome metric)
PANELS = {
    "e1": (["population", "diversity", "mean_size"], "reproductive_success"),
    "e2": (["population", "mean_maturity", "mean_generation"], "mean_population"),
    "e3": (["population", "mean_vision", "mean_speed"], "energy_capture"),
    "e4": (["prey", "predators", "mean_maturity"], "mean_lifespan"),
    "e5": (["population", "mean_size", "mean_speed"], "mean_population"),
    "e6": (["population", "mean_thermalOptimum", "diversity"], "mean_lifespan"),
    "e7": (["population", "mean_vision", "diversity"], "mean_lifespan"),
}
LABELS = {
    "population": "Population", "prey": "Prey", "predators": "Predators", "diversity": "Genetic diversity",
    "mean_generation": "Mean generation", "food_fraction": "Food available",
    "reproductive_success": "Share of organisms that reproduced", "mean_population": "Mean population (2nd half)",
    "energy_capture": "Share of plant growth assimilated", "mean_lifespan": "Mean lifespan (ticks)",
}


def label(col):
    if col in LABELS:
        return LABELS[col]
    if col.startswith("mean_"):
        return "Mean " + col[5:] + " gene"
    return col


def t95(df):
    table = [0, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228]
    return table[df] if df < len(table) else 1.96 + 2.4 / df


def plot(exp_dir: Path):
    summary = json.loads((exp_dir / "summary.json").read_text())
    key = exp_dir.name.split("_")[0]
    cols, outcome = PANELS.get(key, (["population", "diversity", "mean_speed"], "mean_population"))
    runs = list(csv.DictReader(open(exp_dir / "runs.csv")))

    fig, axes = plt.subplots(1, 4, figsize=(17, 3.6), gridspec_kw={"width_ratios": [1, 1, 1, 0.8]})
    for ax, col in zip(axes[:3], cols):
        for i, cond in enumerate(summary["conditions"]):
            ts = cond["timeseries"]
            if col not in ts:
                continue
            x = ts["tick"]
            m, lo, hi = ts[col]["mean"], ts[col]["lo"], ts[col]["hi"]
            c = COLORS[i % len(COLORS)]
            ax.plot(x, [v if v is not None else math.nan for v in m], color=c, lw=1.6, label=cond["name"])
            ax.fill_between(x, [v if v is not None else math.nan for v in lo],
                            [v if v is not None else math.nan for v in hi], color=c, alpha=0.18, lw=0)
        ax.set_title(label(col), fontsize=10)
        ax.set_xlabel("tick")
        ax.grid(alpha=0.25)
    axes[0].legend(fontsize=8, frameon=False)

    ax = axes[3]
    names = [c["name"] for c in summary["conditions"]]
    for i, name in enumerate(names):
        vals = [float(r[outcome]) for r in runs if r["condition"] == name and r[outcome] not in ("", None)]
        c = COLORS[i % len(COLORS)]
        ax.scatter([i + (k - len(vals) / 2) * 0.025 for k in range(len(vals))], vals, s=14, color=c, alpha=0.7)
        if len(vals) >= 2:
            mean = sum(vals) / len(vals)
            sd = math.sqrt(sum((v - mean) ** 2 for v in vals) / (len(vals) - 1))
            h = t95(len(vals) - 1) * sd / math.sqrt(len(vals))
            ax.errorbar(i + 0.28, mean, yerr=h, fmt="o", color="black", ms=4, capsize=3, lw=1.2)
    ax.set_xticks(range(len(names)))
    ax.set_xticklabels(names, rotation=20, ha="right", fontsize=8)
    ax.set_title(label(outcome) + "\n(dots = runs, bar = mean ± 95% CI)", fontsize=9)
    ax.grid(alpha=0.25, axis="y")

    fig.suptitle(f"{summary['experiment']}: {summary['seeds']} seeds × {len(names)} conditions × {summary['ticks']} ticks "
                 f"(bands = 95% CI across seeds)", fontsize=11)
    fig.tight_layout()
    out = exp_dir / "figure.png"
    fig.savefig(out, dpi=110)
    plt.close(fig)
    print("wrote", out)


def main():
    dirs = [Path(a) for a in sys.argv[1:]] or sorted(p for p in Path("results").iterdir() if (p / "summary.json").exists())
    for d in dirs:
        plot(d)


if __name__ == "__main__":
    main()
