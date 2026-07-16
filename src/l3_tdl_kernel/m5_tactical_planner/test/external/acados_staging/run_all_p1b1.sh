#!/usr/bin/env bash
# P1b-1a — sequential T8->T6->T7->T9 staging runner harness (the FINAL P1b-1a
# gate sequence).
#
# P1b-1a adds TWO new complexity points on a NEW dynamics to the P1b-0 four:
#   T8  : VDM-direct c_u identification (the honest double-integrator yaw gain).
#   T6  : double-integrator heading dynamics (Path B) -- the new dynamics.
#   T7  : per-target per-step xi high-dim slack (idxsh=[0,1]).
#   T9  : the FINAL 6-point merge gate (dynamics + 4 P1b-0 points + per-target xi
#         coexist in ONE acatos OCP).
# The sequence runs T8 first (it produces c_u, read by T6/T9 via
# T8_ident/nomoto_params.json), then T6 (dynamics), then T7 (per-target xi), then
# T9 (the merge). Stops on the first FAIL (失败即停).
#
# Each task lives in its own dir and exposes run_<suffix>.sh, where <suffix> is
# the part after "T<n>_" in the dir name, lowercased (T8_ident is special-cased
# to run_ident.sh).
set -euo pipefail

cd "$(dirname "$0")"

# task_dir : run_suffix pairs (T8 first so c_u is available to T6/T9).
TASKS=(
  "T8_ident:ident"
  "T6_doubleint:doubleint"
  "T7_xi:xi"
  "T9_merge6:merge6"
)

for entry in "${TASKS[@]}"; do
  task_dir="${entry%%:*}"
  suffix="${entry##*:}"
  task="${task_dir%%_*}"   # T8, T6, ...
  echo "=== run ${task_dir}/run_${suffix}.sh ==="
  if ! bash "${task_dir}/run_${suffix}.sh"; then
    echo "FAIL at ${task} — stop"
    exit 1
  fi
done

echo "ALL PASS P1b-1a: 6 points staging scalable, P1b-1b 生产 backend 可写"
