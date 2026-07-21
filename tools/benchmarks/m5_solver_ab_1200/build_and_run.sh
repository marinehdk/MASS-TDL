#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${M5_BENCH_IMAGE:-codex-m5-review-sil-nodes:latest}"
OUTPUT_ARG="${1:-runs/m5_solver_ab_$(date +%Y%m%d_%H%M%S)}"
if [[ "$OUTPUT_ARG" = /* ]]; then
  EVIDENCE="$OUTPUT_ARG"
else
  EVIDENCE="$ROOT/$OUTPUT_ARG"
fi
CONTAINER="codex-m5-solver-ab-${USER:-user}-$$"

mkdir -p "$EVIDENCE"
cp "$SCRIPT_DIR/runner_rule14_1200.cpp" "$EVIDENCE/"
cp "$SCRIPT_DIR/run_all.sh" "$EVIDENCE/"
cp "$SCRIPT_DIR/analyze.py" "$EVIDENCE/"

cleanup() {
  docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
}
trap cleanup EXIT

docker run -d --name "$CONTAINER" --entrypoint bash \
  -v "$ROOT:/workspace:ro" \
  -v "$EVIDENCE:/evidence:rw" \
  "$IMAGE" -lc 'sleep infinity' >/dev/null

build_backend() {
  local label="$1"
  local build_dir="$2"
  local acados="$3"
  docker exec "$CONTAINER" bash -lc "
    set -eo pipefail
    rm -rf /tmp/bench_${build_dir}
    mkdir -p /tmp/bench_${build_dir}/src
    cp -a /workspace/src/l3_tdl_kernel/m5_tactical_planner /tmp/bench_${build_dir}/src/
    cp /evidence/runner_rule14_1200.cpp \
      /tmp/bench_${build_dir}/src/m5_tactical_planner/test/external/rule14_ho_benchmark/runner_rule14.cpp
    source /opt/ros/humble/setup.bash
    source /opt/ws/install/setup.bash
    cd /tmp/bench_${build_dir}
    colcon build --packages-select m5_tactical_planner \
      --cmake-target rule14_bench_runner \
      --executor sequential --event-handlers console_direct+ \
      --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
      -DM5_USE_CASADI=ON -DM5_USE_ACADOS=${acados} \
      2>&1 | tee /evidence/build_${label}.log
  "
}

build_backend ipopt off OFF
build_backend acados on ON

CPU_CORE="${BENCH_CPU_CORE:-$(($(nproc) - 1))}"
docker exec \
  -e BENCH_CPU_CORE="$CPU_CORE" \
  -e BENCH_REPEATS="${BENCH_REPEATS:-5}" \
  -e BENCH_WARM_RUNS="${BENCH_WARM_RUNS:-10}" \
  "$CONTAINER" bash /evidence/run_all.sh

python3 "$EVIDENCE/analyze.py" >"$EVIDENCE/analyze_stdout.json"

{
  echo "git_head=$(git -C "$ROOT" rev-parse HEAD)"
  echo "git_branch=$(git -C "$ROOT" branch --show-current)"
  echo "image=$IMAGE"
  echo "cpu_core=$CPU_CORE"
  sha256sum "$EVIDENCE/runner_rule14_1200.cpp" "$EVIDENCE/summary.json"
} >"$EVIDENCE/manifest.txt"

echo "M5 solver A/B evidence: $EVIDENCE"
