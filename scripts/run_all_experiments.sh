#!/usr/bin/env bash
# Runs every experiment in config/experiments/ and writes results/<name>/.
# Usage: scripts/run_all_experiments.sh [jobs] [evosim-binary]
set -euo pipefail
cd "$(dirname "$0")/.."
JOBS="${1:-$(nproc 2>/dev/null || echo 2)}"
BIN="${2:-build/evosim}"
for spec in config/experiments/*.json; do
  name="$(basename "$spec" .json)"
  "$BIN" experiment "$spec" --jobs "$JOBS" --out "results/$name" > "results/$name.log" 2>&1 || { echo "FAILED: $name (see results/$name.log)"; exit 1; }
  echo "done: $name ($(grep 'finished in' "results/$name.log" | sed 's/^ *//'))"
done
