#!/usr/bin/env bash
# A4000 headless RTF envelope sweep. Reuses rtf_headless_sweep.py but with
# per-rate sample windows because the 295s-scenario ends quickly at high rate.
# Output: a clean table of {nominal, measured RTF, efficiency} for 5/10/20/30/50x.
set -e
cd "$(dirname "$0")/.."
source scripts/a4000-env.sh

# For 5/10: scenario runs ~295s sim, sample 20s wall mid-run
# For 20/30: scenario ends in ~15s, sample 4s
# For 50: scenario ends in ~6s, sample 2s
# For 100: scenario ends in ~3s, sample 1s (likely at the CPU ceiling)
for spec in "5 4 20 1.0" "10 4 20 1.0" "20 1 4 0.25" "30 1 4 0.25" "50 1 2 0.15"; do
  read R SETTLE WIN INT <<< "$spec"
  echo "=== rate ${R}x (settle=${SETTLE}s window=${WIN}s interval=${INT}s) ==="
  SETTLE_S=$SETTLE SAMPLE_S=$WIN INTERVAL=$INT \
    python3 scripts/rtf_headless_sweep.py colreg-rule14-ho $R 2>&1 | tail -3
  echo
done
