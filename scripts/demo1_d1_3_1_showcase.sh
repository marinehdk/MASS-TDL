#!/usr/bin/env bash
# SPDX-License-Identifier: Proprietary
# scripts/demo1_d1_3_1_showcase.sh
#
# DEMO-1 three-segment showcase (D1.3.1 MMG Integration)
#
# Segments:
#   1  AIS 1h data playback + parse rate
#   2  ShipMotionSimulator abstract plugin switching
#   3  CI lint cross-import demo (RL-isolation boundary)
#
# Usage:  ./scripts/demo1_d1_3_1_showcase.sh [--record]
#
#   --record  tee terminal output into evidence/demo1_d1_3_1/
#
set -uo pipefail

# ──────────────────────────────────────────────
# helpers
# ──────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

EVIDENCE_DIR="${PROJECT_ROOT}/evidence/demo1_d1_3_1"

FAILS=0
PASSES=0

banner() {
    local msg="$1"
    local sep
    sep="$(printf '─%.0s' $(seq 1 72))"
    echo ""
    echo "  ${sep}"
    printf "  ═══ %s ═══\n" "${msg}"
    echo "  ${sep}"
    echo ""
}

ok()   { echo "  ✅ $1"; PASSES=$((PASSES + 1)); }
fail() { echo "  ❌ $1"; FAILS=$((FAILS + 1)); }

# ──────────────────────────────────────────────
# option parsing
# ──────────────────────────────────────────────
RECORD=0
for arg in "$@"; do
    case "${arg}" in
        --record) RECORD=1 ;;
        *) echo "Unknown option: ${arg}" >&2; exit 2 ;;
    esac
done

if [[ ${RECORD} -eq 1 ]]; then
    mkdir -p "${EVIDENCE_DIR}"
    LOGFILE="${EVIDENCE_DIR}/showcase-$(date +%Y%m%d-%H%M%S).log"
    echo "Recording to ${LOGFILE}"
    # reopen stdout+stderr through tee
    exec > >(tee -a "${LOGFILE}") 2>&1
fi

# ──────────────────────────────────────────────
# precondition check
# ──────────────────────────────────────────────
cd "${PROJECT_ROOT}"
echo "Project root: ${PROJECT_ROOT}"
echo ""

# ═══════════════════════════════════════════════
# SEGMENT 1: AIS 1h data playback + parse rate
# ═══════════════════════════════════════════════
banner "SEGMENT 1 — AIS 1h Data Playback + Parse Rate"

# --- 1a: dataset info ---
AIS_CSV="${PROJECT_ROOT}/data/ais_datasets/AIS_synthetic_1h.csv"
echo "--- Dataset info ---"
if [[ -f "${AIS_CSV}" ]]; then
    wc -l "${AIS_CSV}"
    ls -lh "${AIS_CSV}"
    ok "AIS dataset found"
else
    echo "Dataset missing — attempting download ..."
    python3 "${PROJECT_ROOT}/src/sim_workbench/ais_bridge/scripts/download_dataset.py" \
        --source synthetic \
        --output-dir "${PROJECT_ROOT}/data/ais_datasets/"
    if [[ -f "${AIS_CSV}" ]]; then
        ok "AIS dataset downloaded"
    else
        fail "AIS dataset download failed"
    fi
fi

# --- 1b: parse rate test ---
echo ""
echo "--- Parse rate test ---"
if python3 -m pytest "${PROJECT_ROOT}/src/sim_workbench/ais_bridge/test/test_parse_rate.py" -v; then
    ok "Parse rate test passed"
else
    fail "Parse rate test failed"
fi

# --- 1c: replay rate config ---
echo ""
echo "--- Replay rate config ---"
REPLAY_NODE="${PROJECT_ROOT}/src/sim_workbench/ais_bridge/ais_bridge/replay_node.py"
if [[ -f "${REPLAY_NODE}" ]]; then
    grep -n "replay_rate_x" "${REPLAY_NODE}"
    ok "Replay rate config shown"
else
    fail "replay_node.py not found at ${REPLAY_NODE}"
fi

# ═══════════════════════════════════════════════
# SEGMENT 2: ShipMotionSimulator abstract plugin switching
# ═══════════════════════════════════════════════
banner "SEGMENT 2 — ShipMotionSimulator Abstract Plugin Switching"

# --- 2a: ABC interface ---
echo "--- ABC interface ---"
ABC_FILE="${PROJECT_ROOT}/src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/ship_motion_simulator.py"
if [[ -f "${ABC_FILE}" ]]; then
    grep -n "class ShipMotionSimulator\|def step\|def vessel_class\|def export_fmu_interface" "${ABC_FILE}"
    ok "ABC interface shown"
else
    fail "ship_motion_simulator.py not found"
fi

# --- 2b: FCBPlugin instantiation ---
echo ""
echo "--- FCBPlugin instantiation ---"
FCB_PLUGIN_DIR="${PROJECT_ROOT}/src/sim_workbench/sil_nodes/ship_dynamics"
python3 -c "
import sys
sys.path.insert(0, '${FCB_PLUGIN_DIR}')
from ship_dynamics.fcb_plugin import FCBPlugin
p = FCBPlugin()
print(f'  vessel_class = {p.vessel_class()}')
print(f'  hull_class   = {p.hull_class()}')
d = p.export_fmu_interface()
print(f'  FMI model    = {d.model_name}')
print(f'  FMI vars     = {len(d.variables)} vars')
" && ok "FCBPlugin instantiation verified" || fail "FCBPlugin instantiation failed"

# --- 2c: YAML vessel_class ---
echo ""
echo "--- YAML vessel_class ---"
YAML_FILE="${PROJECT_ROOT}/src/sim_workbench/sil_nodes/ship_dynamics/config/fcb_dynamics.yaml"
if [[ -f "${YAML_FILE}" ]]; then
    grep -A2 "vessel_class" "${YAML_FILE}"
    ok "Vessel class YAML config shown"
else
    fail "fcb_dynamics.yaml not found"
fi

# --- 2d: Plugin registry ---
echo ""
echo "--- Plugin registry ---"
NODE_FILE="${PROJECT_ROOT}/src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py"
if [[ -f "${NODE_FILE}" ]]; then
    grep -A8 "_PLUGIN_REGISTRY" "${NODE_FILE}" | head -10
    ok "Plugin registry shown"
else
    fail "node.py not found"
fi

# ═══════════════════════════════════════════════
# SEGMENT 3: CI lint cross-import demo
# ═══════════════════════════════════════════════
banner "SEGMENT 3 — CI Lint Cross-Import Demo (RL-Isolation Boundary)"

CI_CHECK="${PROJECT_ROOT}/tools/ci/check-rl-isolation.sh"
VIOLATION_SRC="${PROJECT_ROOT}/tools/ci/demo/violation_fixture.cpp"
VIOLATION_DST="${PROJECT_ROOT}/src/sim_workbench/fcb_simulator/src/_demo_violation.cpp"

if [[ ! -x "${CI_CHECK}" ]]; then
    fail "check-rl-isolation.sh not found or not executable"
    # skip segment 3 if infra missing
else
    # --- Phase 1: clean lint ---
    echo "--- Phase 1: clean lint (should PASS) ---"
    if bash "${CI_CHECK}"; then
        ok "Phase 1 — clean lint passed (no violations)"
    else
        fail "Phase 1 — clean lint unexpectedly failed"
    fi

    # --- Phase 2: inject violation ---
    echo ""
    echo "--- Phase 2: inject violation (should FAIL with VIOLATION) ---"
    if [[ -f "${VIOLATION_SRC}" ]]; then
        cp "${VIOLATION_SRC}" "${VIOLATION_DST}"
        echo "  Injected: ${VIOLATION_DST}"

        if bash "${CI_CHECK}"; then
            fail "Phase 2 — lint passed but SHOULD have detected violation"
        else
            # Check that VIOLATION message actually appeared
            # Re-run and capture output to verify
            OUTPUT=$(bash "${CI_CHECK}" 2>&1)
            if echo "${OUTPUT}" | grep -q "VIOLATION"; then
                ok "Phase 2 — lint correctly flagged VIOLATION"
            else
                fail "Phase 2 — lint failed but no VIOLATION message found"
            fi
        fi
    else
        fail "violation_fixture.cpp not found at ${VIOLATION_SRC}"
    fi

    # --- Phase 3: remove violation, verify clean ---
    echo ""
    echo "--- Phase 3: remove violation (should PASS again) ---"
    rm -f "${VIOLATION_DST}"
    if bash "${CI_CHECK}"; then
        ok "Phase 3 — post-removal lint passed (boundary restored)"
    else
        fail "Phase 3 — lint still failing after violation removal"
    fi
fi

# ──────────────────────────────────────────────
# summary
# ──────────────────────────────────────────────
banner "SUMMARY"
echo "  Passed: ${PASSES}"
echo "  Failed: ${FAILS}"
if [[ ${FAILS} -eq 0 ]]; then
    echo ""
    echo "  🎉 All segments PASSED — DEMO-1 D1.3.1 showcase ready."
    echo ""
    exit 0
else
    echo ""
    echo "  ⚠️  ${FAILS} check(s) failed — review output above."
    echo ""
    exit 1
fi
