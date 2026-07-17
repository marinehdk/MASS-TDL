#!/usr/bin/env bash
# P1b-1c Task 19 (rescoped) — Rule14 head-on benchmark: IPOPT (M5_USE_ACADOS=OFF)
# vs acatos (M5_USE_ACADOS=ON) on TWO scenarios:
#   (1) PRIMARY: target at 2000 m head-on (production-realistic mid-MPC early-
#       detection envelope, TCPA ~200 s) — the 6 behavior-equivalence gates.
#   (2) DOCUMENTED LIMITATION: target at 500 m head-on (TCPA ~50 s) — NOT a
#       gate; recorded for the acatos short-TCPA QP-conditioning finding (T20
#       promotability input).
#
# INVOCATION: this script runs INSIDE the sil-nodes container. The host-side
# wrapper that builds/starts the container and enters it is documented in the
# task-19-report §3 (COMPOSE_PROJECT_NAME=codex-acados-t19-rescop). The container
# is non-persistent for build/ and install/ (only src/ scenarios/ runs/ docker/
# are bind-mounted), so ALL builds + runs + compares happen in ONE script
# session.
#
# FAILURE DISCIPLINE (NON-NEGOTIABLE): no mocks, no skips, no forced-pass, no
# threshold-tuning. If IPOPT fails to converge the PRIMARY (2000m) head-on
# scenario in the OFF build, STOP and report BLOCKED (the benchmark cannot
# compare a non-converging IPOPT on the primary scenario). The 500m limitation
# datapoint is recorded whatever acatos does there (the finding IS the point).
# If a gate fails in compare.py, classify physics-difference vs bug per spec
# §P1b-1c failure-handling — do NOT widen tolerance silently.
# NOTE: no `set -u` until AFTER sourcing ROS — /opt/ros/humble/setup.bash
# references unbound vars under `set -u` (AMENT_TRACE_SETUP_FILES).
set -eo pipefail

# Locate the benchmark dir: this script lives at test/external/rule14_ho_benchmark/.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PKG_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"  # m5_tactical_planner/
BENCH_DIR="${SCRIPT_DIR}"

echo "=== Rule14 HO benchmark (P1b-1c, rescoped to production-realistic 2000m) ==="
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

# ---- helper: build + run one backend on one scenario distance, capture JSON ----
# args: backend(ipopt|acatos)  json_out  acados_flag(OFF|ON)  target_distance_m
build_and_run() {
  local backend="$1"     # ipopt | acatos
  local json_out="$2"
  local acados_flag="$3" # OFF | ON
  local target_dist="$4" # 2000 (primary) | 500 (limitation)
  local runner_path

  echo "=== [build ${backend} @ ${target_dist}m] "
  echo "         M5_USE_ACADOS=${acados_flag} SCENARIO_TARGET_DISTANCE_M=${target_dist} ==="
  # --cmake-clean-cache: the M5_USE_ACADOS flag AND the SCENARIO_TARGET_DISTANCE_M
  # compile-def MUST flip cleanly. If a prior build cached either in CMakeCache.txt,
  # an incremental colcon build silently reuses it (T17/T18 pitfall). Clean cache
  # on every flip. SCENARIO_TARGET_DISTANCE_M is threaded via the CMake var
  # RULE14_SCENARIO_TARGET_DISTANCE_M -> runner target_compile_definitions.
  colcon build --packages-select m5_tactical_planner \
    --cmake-clean-cache \
    --cmake-args -DM5_USE_ACADOS="${acados_flag}" \
                 -DRULE14_SCENARIO_TARGET_DISTANCE_M="${target_dist}" \
                 -DCMAKE_BUILD_TYPE=Release \
    >/dev/null

  # The runner executable lands at build/<pkg>/rule14_bench_runner.
  runner_path="${WS_DIR}/build/m5_tactical_planner/rule14_bench_runner"
  if [ ! -x "${runner_path}" ]; then
    echo "FAIL: ${backend} build did not produce ${runner_path}" >&2
    exit 3
  fi

  echo "=== [run   ${backend} @ ${target_dist}m] rule14_bench_runner > ${json_out} ==="
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

# =============================================================================
# (1) PRIMARY scenario: 2000 m head-on — the 6 behavior-equivalence gates.
# =============================================================================
PRIMARY_DIST="2000"
IPOPT_PRIMARY="${OUTPUT_DIR}/ipopt_rule14_${PRIMARY_DIST}m.json"
ACADOS_PRIMARY="${OUTPUT_DIR}/acados_rule14_${PRIMARY_DIST}m.json"

echo "################ PRIMARY SCENARIO: ${PRIMARY_DIST}m head-on (TCPA ~200s) ################"
echo "# production-realistic mid-MPC early-detection envelope. acatos converges"
echo "# here (experiment A: 2000m/1852m → Converged, sqp_iter=11, CPA=1234.9m, 30ms)."
echo

# ---- 1a. IPOPT build + run (M5_USE_ACADOS=OFF) on PRIMARY ----
# BLOCKED-CHECK: if IPOPT fails to converge the PRIMARY (2000m) head-on scenario,
# STOP. Per the task brief: the benchmark cannot compare a non-converging IPOPT
# on the primary scenario — that is a BLOCKED (container-IPOPT env issue), NOT a
# forced-pass.
build_and_run ipopt "${IPOPT_PRIMARY}" OFF "${PRIMARY_DIST}"

IPOPT_USABLE="$(python3 -c "import json; print(json.load(open('${IPOPT_PRIMARY}'))['usable'])")"
if [ "${IPOPT_USABLE}" != "True" ]; then
  echo "================================================================" >&2
  echo "BLOCKED: IPOPT did NOT produce a usable PRIMARY (${PRIMARY_DIST}m)" >&2
  echo "         Rule14 head-on trajectory. (container IPOPT/MUMPS env" >&2
  echo "         issue — not a benchmark bug.) The benchmark cannot compare" >&2
  echo "         a non-converging IPOPT on the primary scenario." >&2
  echo "         Per task-19-rescop-brief: STOP, report BLOCKED." >&2
  echo "================================================================" >&2
  exit 5
fi
echo "=== [gate] IPOPT usable on PRIMARY ${PRIMARY_DIST}m Rule14 head-on — proceeding to acatos ==="
echo

# ---- 1b. acatos build + run (M5_USE_ACADOS=ON) on PRIMARY ----
build_and_run acados "${ACADOS_PRIMARY}" ON "${PRIMARY_DIST}"

# ---- 1c. compare.py — 6 behavior-equivalence gates on PRIMARY ----
# NOTE: compare.py returns the hard-gate failure count as its exit code (non-zero
# if any hard gate FAILs). We must NOT let that abort the script under
# `set -eo pipefail` — the limitation scenario MUST still run, and a gate FAIL
# is a REPORTED finding, not a script failure. Capture the exit code explicitly.
echo "=== [compare PRIMARY] 6 gates on ${PRIMARY_DIST}m ==="
set +e +o pipefail
python3 "${BENCH_DIR}/compare.py" "${IPOPT_PRIMARY}" "${ACADOS_PRIMARY}" \
  2>&1 | tee "${OUTPUT_DIR}/compare_primary_${PRIMARY_DIST}m.txt"
PRIMARY_COMPARE_RC="${PIPESTATUS[0]}"
set -eo pipefail
echo "=== [compare PRIMARY] exit code = ${PRIMARY_COMPARE_RC} (hard-gate failure count; 0 = all PASS) ==="
echo

# =============================================================================
# (2) DOCUMENTED LIMITATION scenario: 500 m head-on — recorded datapoint (NOT a
#     gate). The finding: acatos Path B cannot condition the first QP from a
#     straight-hold seed when TCPA < ~200s (status=4, sqp_iter=1). IPOPT solves
#     it via MA57 inertia correction. This is INPUT for T20's M5-M7 fallback
#     boundary decision, NOT a gate that blocks the benchmark.
# =============================================================================
LIMIT_DIST="500"
IPOPT_LIMIT="${OUTPUT_DIR}/ipopt_rule14_${LIMIT_DIST}m.json"
ACADOS_LIMIT="${OUTPUT_DIR}/acados_rule14_${LIMIT_DIST}m.json"

echo "################ DOCUMENTED LIMITATION: ${LIMIT_DIST}m head-on (TCPA ~50s) ################"
echo "# NOT a gate. acatos Path B cannot solve <200s TCPA head-on (QP conditioning"
echo "# at the straight-hold seed, NOT Hessian — GAUSS_NEWTON + soft-CPA fail"
echo "# byte-identically per experiment B). IPOPT handles it via MA57 inertia"
echo "# correction. Recorded for T20's M5-M7 fallback boundary decision."
echo

# ---- 2a. IPOPT build + run on LIMITATION ----
# NOTE: no BLOCKED-check here. IPOPT is EXPECTED to handle 500m (MA57 inertia
# correction); if it somehow fails, we still record whatever it produces — the
# finding is the acatos side, and the IPOPT side is the reference that shows
# the scenario IS solvable by a robust NLP solver.
build_and_run ipopt "${IPOPT_LIMIT}" OFF "${LIMIT_DIST}"

# ---- 2b. acatos build + run on LIMITATION ----
build_and_run acados "${ACADOS_LIMIT}" ON "${LIMIT_DIST}"

# ---- 2c. compare.py — DOCUMENTED LIMITATION report (no gates) ----
# compare.py in limitation mode always returns 0 (the limitation is documented,
# not gated), but capture the exit code anyway for symmetry with the primary.
echo "=== [compare LIMITATION] documented short-TCPA datapoint on ${LIMIT_DIST}m ==="
set +e +o pipefail
python3 "${BENCH_DIR}/compare.py" "${IPOPT_LIMIT}" "${ACADOS_LIMIT}" \
  2>&1 | tee "${OUTPUT_DIR}/compare_limitation_${LIMIT_DIST}m.txt"
LIMIT_COMPARE_RC="${PIPESTATUS[0]}"
set -eo pipefail
echo "=== [compare LIMITATION] exit code = ${LIMIT_COMPARE_RC} (always 0 — limitation recorded, not gated) ==="
echo

echo "=== Rule14 HO benchmark (rescoped) complete ==="
echo "PRIMARY ${PRIMARY_DIST}m evidence     : ${OUTPUT_DIR}/compare_primary_${PRIMARY_DIST}m.txt"
echo "LIMITATION ${LIMIT_DIST}m evidence    : ${OUTPUT_DIR}/compare_limitation_${LIMIT_DIST}m.txt"
echo "raw trajectory JSONs                  : ${OUTPUT_DIR}/{ipopt,acados}_rule14_*m.json"
