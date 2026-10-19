#!/usr/bin/env python3
"""Grid-sweep predator parameters and report how often predators and prey coexist.

Used to choose the documented predator-prey preset (config/predator_prey.json).
Usage: scripts/sweep_predators.py [--evosim build/evosim] [--ticks 20000] [--seeds 3] [--jobs 2]
"""
import argparse
import itertools
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor

GRID = {
    "predators.storageMultiplier": [2.0, 3.0],
    "predators.meatAssimilation": [1.3, 1.8],
    "predators.captureBase": [0.45, 0.8],
}
LINE = re.compile(r"^tick\s+(\d+) \| pop\s+\d+ \(prey\s+(\d+), pred\s+(\d+)\)")


def run(evosim, ticks, seed, overrides, extra):
    sets = ",".join(f"{k}={v}" for k, v in overrides.items())
    if extra:
        sets += "," + extra
    out = subprocess.run([evosim, "run", "--ticks", str(ticks), "--print-every", str(ticks // 10),
                          "--seed", str(seed), "--set", "predators.enabled=true," + sets],
                         capture_output=True, text=True, check=True).stdout
    traj = [(int(m[1]), int(m[2]), int(m[3])) for m in map(LINE.match, out.splitlines()) if m]
    last = traj[-1] if traj else (0, 0, 0)
    reached = last[0] >= ticks
    return {"coexist": reached and last[1] > 0 and last[2] > 0, "prey": last[1] if reached else 0,
            "pred": last[2] if reached else 0, "allDead": not reached}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--evosim", default="build/evosim")
    ap.add_argument("--ticks", type=int, default=20000)
    ap.add_argument("--seeds", type=int, default=3)
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--extra", default="", help="additional --set overrides")
    a = ap.parse_args()
    keys = list(GRID)
    combos = [dict(zip(keys, vals)) for vals in itertools.product(*GRID.values())]
    tasks = [(c, s) for c in combos for s in range(1, a.seeds + 1)]
    with ThreadPoolExecutor(a.jobs) as ex:
        results = list(ex.map(lambda t: run(a.evosim, a.ticks, t[1], t[0], a.extra), tasks))
    print(" | ".join(k.split(".")[1] for k in keys) + " | coexist  allDead  mean prey/pred at end")
    for i, c in enumerate(combos):
        rs = results[i * a.seeds:(i + 1) * a.seeds]
        co = sum(r["coexist"] for r in rs)
        dead = sum(r["allDead"] for r in rs)
        prey = sum(r["prey"] for r in rs) / len(rs)
        pred = sum(r["pred"] for r in rs) / len(rs)
        print(" | ".join(f"{c[k]:>6}" for k in keys) + f" |   {co}/{len(rs)}      {dead}/{len(rs)}    {prey:7.0f} / {pred:5.0f}")


if __name__ == "__main__":
    main()
