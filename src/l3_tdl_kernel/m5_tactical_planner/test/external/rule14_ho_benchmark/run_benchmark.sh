#!/usr/bin/env bash
# P1b-1c Task 19 — Rule14 head-on benchmark: IPOPT (M5_USE_ACADOS=OFF) vs
# acatos (M5_USE_ACADOS=ON). Standard head-on Rule14 geometry; 6 behavior-
# equivalence gates applied by compare.py (DP-05/VR-05 empirical landing).
#
# INVOCATION: this script runs INSIDE the sil-nodes container. The host-side
# wrapper that builds/starts the container and enters it is documented in the
# task-19-report §3 (COMPOSE_PROJECT_NAME=codex-acados-t19). The container is
# non-persistent for build/ and install/ (only src/ scenarios/ runs/ docker/ are
# bind-mounted), so BOTH builds + runs + compare happen in ONE script session.
#
# FAILURE DISCIPLINE (NON-NEGOTIABLE): no mocks, no skips, no forced-pass, no
# threshold-tuning. If IPOPT fails to converge the Rule14 head-on scenario in
# the OFF build (env issue), the benchmark cannot compare → STOP, report BLOCKED
# (the TDL Lead decides whether to use the mass-l3-sil stack for the IPOPT side
# or rescope). If a gate fails in compare.py, classify physics-difference vs
# bug per spec §P1b-1c failure-handling — do NOT widen tolerance silently.
# NOTE: no `set -u` until AFTER sourcing ROS — /opt/ros/humble/setup.bash
# references unbound vars under `set -u` (AMENT_TRACE_SETUP_FILES).
set -eo pipefail

# Locate the benchmark dir: this script lives at test/external/rule14_ho_benchmark/.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"  # m5_tactical_planner/
BENCH_DIR="${SCRIPT_DIR}"

echo "=== Rule14 HO benchmark (P1b-1c) ==="
echo "benchmark dir : ${BENCH_DIR}"
echo "package root  : ${PKG_ROOT}"
echo "ws            : ${WS_DIR:-/opt/ws}"
echo

# Colcon workspace root: the container mounts the repo at /opt/ws/src/l3_tdl_kernel
# via the sil-nodes bind-mount (see docker-compose.yml sil-nodes volumes). Default
# to /opt/ws if the caller did not export WS_DIR.
WS_DIR="${WS_DIR:-/opt/ws}"
if [ ! -d "${WS_DIR}/src/l3_tdl_kernel/m5_tactical_planner" ]; then
  echo "FAIL: m5_tactical_planner not found under ${WS_DIR}/src/l3_tdl_kernel" >&2
  echo "      (is the repo bind-mounted into the container?)" >&2
  exit 2
fi

# ROS env (CasADi + the acatos shared libs are on /usr/local + the codegen dir).
if [ -f /opt/ros/humble/setup.bash ]; then
  # shellcheck disable=SC1091
  source /opt/ros/humble/setup.bash
elif [ -f /opt/ros/jazzy/setup.bash ]; then
  # shellcheck disable=SC1091
  source /opt/ros/jazzy/setup.bash
fi
set -u   # re-enable unbound-var guard now that ROS setup has run
cd "${WS_DIR}"

OUTPUT_DIR="${BENCH_DIR}/out"
mkdir -p "${OUTPUT_DIR}"
IPOPT_JSON="${OUTPUT_DIR}/ipopt_rule14.json"
ACADOS_JSON="${OUTPUT_DIR}/acados_rule14.json"

# ---- helper: build + run one backend, capture the runner stdout ----
build_and_run() {
  local backend="$1"   # ipopt | acatos
  local json_out="$2"
  local acados_flag="$3"
  local runner_path

  echo "=== [build ${backend}] M5_USE_ACADOS=${acados_flag} ==="
  # --cmake-clean-cache: the M5_USE_ACADOS flag MUST flip cleanly. If a prior
  # build cached M5_USE_ACADOS=OFF in CMakeCache.txt, an incremental colcon
  # build silently reuses it (T17/T18 pitfall). Clean cache on every flip.
  colcon build --packages-select m5_tactical_planner \
    --cmake-clean-cache \
    --cmake-args -DM5_USE_ACADOS="${acados_flag}" -DCMAKE_BUILD_TYPE=Release \
    >/dev/null

  # The runner executable lands at build/<pkg>/rule14_bench_runner.
  runner_path="${WS_DIR}/build/m5_tactical_planner/rule14_bench_runner"
  if [ ! -x "${runner_path}" ]; then
    echo "FAIL: ${backend} build did not produce ${runner_path}" >&2
    exit 3
  fi

  echo "=== [run   ${backend}] rule14_bench_runner > ${json_out} ==="
  # The runner writes JSON to stdout; diagnostics to stderr. The IPOPT banner
  # (CasADi dlopen on first call) ALSO goes to stdout (the IPOPT library prints
  # it directly, not via stderr; print_level=0 suppresses per-iter output but
  # NOT the startup banner). So the raw stdout = banner + JSON. We capture the
  # raw stdout, then EXTRACT the JSON object (the last top-level {...} block)
  # via the _extract_json.py helper so compare.py receives clean JSON.
  local raw_out="${json_out}.raw"
  "${runner_path}" > "${raw_out}" 2> "${json_out}.stderr"
  python3 "${BENCH_DIR}/_extract_json.py" "${raw_out}" > "${json_out}"
  # Validate the extracted JSON is parseable.
  if ! python3 "${BENCH_DIR}/_summarize.py" "${json_out}" >/dev/null 2>&1; then
    echo "FAIL: ${backend} runner did not produce valid JSON (${json_out})" >&2
    echo "      raw stdout tail:" >&2
    tail -n 20 "${raw_out}" >&2 || true
    echo "      stderr tail:" >&2
    tail -n 20 "${json_out}.stderr" >&2 || true
    exit 4
  fi

  # Quick echo of the backend + status so the log is human-readable.
  python3 "${BENCH_DIR}/_summarize.py" "${json_out}"
  echo
}

# ---- 1. IPOPT build + run (M5_USE_ACADOS=OFF) ----
# BLOCKED-CHECK: if IPOPT fails to converge the Rule14 head-on scenario in this
# container, STOP. Per the task brief: the benchmark cannot compare a non-
# converging IPOPT — that is a BLOCKED (container-IPOPT env issue), NOT a
# forced-pass. The TDL Lead decides whether to use the mass-l3-sil stack for
# the IPOPT side or rescope.
build_and_run ipopt "${IPOPT_JSON}" OFF

# Inspect IPOPT usability — the BLOCKED gate.
IPOPT_USABLE="$(python3 -c "import json; print(json.load(open('${IPOPT_JSON}'))['usable'])")"
if [ "${IPOPT_USABLE}" != "True" ]; then
  echo "================================================================" >&2
  echo "BLOCKED: IPOPT did NOT produce a usable Rule14 head-on trajectory." >&2
  echo "         (container IPOPT/MUMPS env issue — not a benchmark bug.)" >&2
  echo "         The benchmark cannot compare a non-converging IPOPT." >&2
  echo "         Per task-19-brief: STOP, report BLOCKED." >&2
  echo "================================================================" >&2
  exit 5
fi
echo "=== [gate] IPOPT usable on Rule14 head-on — proceeding to acatos ==="
echo

# ---- 2. acatos build + run (M5_USE_ACADOS=ON) ----
build_and_run acados "${ACADOS_JSON}" ON

# ---- 3. compare.py — 6 behavior-equivalence gates ----
echo "=== [compare] 6 gates ==="
python3 "${BENCH_DIR}/compare.py" "${IPOPT_JSON}" "${ACADOS_JSON}" \
  | tee "${OUTPUT_DIR}/compare_result.txt"

echo
echo "=== Rule14 HO benchmark complete ==="
echo "evidence: ${OUTPUT_DIR}/"
