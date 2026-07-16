#!/usr/bin/env bash
# P1b-0 T0 — sequential T1->T5 staging runner harness.
#
# Each staging task lives in its own dir and exposes run_<suffix>.sh, where
# <suffix> is the part after "T<n>_" in the dir name, lowercased:
#   T1_prefix  -> prefix
#   T2_colreg  -> colreg
#   T3_slack   -> slack
#   T4_bounds  -> bounds
#   T5_merged  -> merged
# The task dirs do not exist at T0 time; this script is exercised at T6.
set -euo pipefail

cd "$(dirname "$0")"

# task_dir : run_suffix pairs.
TASKS=(
  "T1_prefix:prefix"
  "T2_colreg:colreg"
  "T3_slack:slack"
  "T4_bounds:bounds"
  "T5_merged:merged"
)

for entry in "${TASKS[@]}"; do
  task_dir="${entry%%:*}"
  suffix="${entry##*:}"
  task="${task_dir%%_*}"   # T1, T2, ...
  echo "=== run ${task_dir}/run_${suffix}.sh ==="
  if ! bash "${task_dir}/run_${suffix}.sh"; then
    echo "FAIL at ${task} — stop"
    exit 1
  fi
done

echo "ALL PASS: staging scalable, P1b-1 全量 spec 可写"
