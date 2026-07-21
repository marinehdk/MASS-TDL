#!/usr/bin/env bash
set -eo pipefail

EVIDENCE=/evidence
IPOPT_BIN=/tmp/bench_off/build/m5_tactical_planner/rule14_bench_runner
ACADOS_BIN=/tmp/bench_on/build/m5_tactical_planner/rule14_bench_runner
CORE="${BENCH_CPU_CORE:-15}"
REPEATS="${BENCH_REPEATS:-5}"
WARM_RUNS="${BENCH_WARM_RUNS:-10}"

source /opt/ros/humble/setup.bash
source /opt/ws/install/setup.bash
set -u
export OMP_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1
export MKL_NUM_THREADS=1

run_one() {
  local backend="$1"
  local rep="$2"
  local binary
  if [[ "$backend" == "ipopt" ]]; then
    binary="$IPOPT_BIN"
  else
    binary="$ACADOS_BIN"
  fi
  local log="$EVIDENCE/${backend}_rep${rep}.log"
  local json="$EVIDENCE/${backend}_rep${rep}.json"
  local wall="$EVIDENCE/${backend}_rep${rep}.process_wall_s"
  echo "RUN backend=${backend} rep=${rep} core=${CORE} warm_runs=${WARM_RUNS}"
  local begin_ns end_ns
  begin_ns="$(date +%s%N)"
  env BENCH_WARM_RUNS="$WARM_RUNS" taskset -c "$CORE" "$binary" >"$log" 2>&1
  end_ns="$(date +%s%N)"
  awk -v begin="$begin_ns" -v end="$end_ns" 'BEGIN { printf "%.9f\n", (end-begin)/1000000000.0 }' >"$wall"
  tail -n 1 "$log" >"$json"
  python3 -m json.tool "$json" >/dev/null
}

for rep in $(seq 1 "$REPEATS"); do
  if (( rep % 2 == 1 )); then
    run_one ipopt "$rep"
    run_one acados "$rep"
  else
    run_one acados "$rep"
    run_one ipopt "$rep"
  fi
done

echo "DONE repeats=${REPEATS} warm_runs=${WARM_RUNS}"
