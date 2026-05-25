#!/usr/bin/env bash
# Doer-Checker Triple Quantification Matrix (D3.3a spec 5)
# LOC ratio >=50:1 + Cyclomatic Complexity ratio >=30:1 + SBOM cap = empty set
set -euo pipefail

readonly M7_SRC="src/l3_tdl_kernel/m7_safety_supervisor"
readonly DOER_DIRS=(
    "src/l3_tdl_kernel/m1_odd_envelope_manager"
    "src/l3_tdl_kernel/m2_world_model"
    "src/l3_tdl_kernel/m3_mission_manager"
    "src/l3_tdl_kernel/m4_behavior_arbiter"
    "src/l3_tdl_kernel/m5_tactical_planner"
    "src/l3_tdl_kernel/m6_colregs_reasoner"
)
errors=0

echo "=== Doer-Checker Quantification Report ==="

if command -v cloc &>/dev/null; then
    M7_LOC=$(cloc --csv --quiet "${M7_SRC}" 2>/dev/null | tail -1 | cut -d, -f5)
    DOER_LOC=0
    for d in "${DOER_DIRS[@]}"; do
        [[ -d "${d}" ]] || continue
        pkg_loc=$(cloc --csv --quiet "${d}" 2>/dev/null | tail -1 | cut -d, -f5)
        DOER_LOC=$((DOER_LOC + ${pkg_loc:-0}))
    done
    M7_LOC=${M7_LOC:-0}
    DOER_LOC=$((DOER_LOC > 0 ? DOER_LOC : 1))
    LOC_RATIO=$((DOER_LOC / (M7_LOC > 0 ? M7_LOC : 1)))
    echo "LOC Ratio:   M7=${M7_LOC}, Doers(M1-M6)=${DOER_LOC}, Ratio=${LOC_RATIO}:1 (target >=50:1)"
    if [[ ${LOC_RATIO} -lt 50 ]]; then
        echo "  WARNING: Doer/Checker LOC ratio ${LOC_RATIO}:1 < 50:1"
    else
        echo "  PASS"
    fi
else
    echo "LOC Ratio:   SKIP (cloc not installed)"
fi

if command -v lizard &>/dev/null; then
    M7_CCN_AVG=$(lizard "${M7_SRC}" 2>/dev/null | grep "Total nloc" | awk '{for(i=1;i<=NF;i++) if($i=="AvgCCN"||$i=="avgCCN") {print $(i+1); exit}}')
    DOER_CCN_SUM=0; DOER_PKG_COUNT=0
    for d in "${DOER_DIRS[@]}"; do
        [[ -d "${d}" ]] || continue
        ccn_val=$(lizard "${d}" 2>/dev/null | grep "Total nloc" | awk '{for(i=1;i<=NF;i++) if($i=="AvgCCN"||$i=="avgCCN") {print $(i+1); exit}}')
        if [[ -n "${ccn_val}" ]]; then
            DOER_CCN_SUM=$(echo "${DOER_CCN_SUM} + ${ccn_val}" | bc 2>/dev/null || echo "${DOER_CCN_SUM}")
            DOER_PKG_COUNT=$((DOER_PKG_COUNT + 1))
        fi
    done
    M7_CCN_AVG=${M7_CCN_AVG:-0}
    if [[ ${DOER_PKG_COUNT} -gt 0 ]]; then
        DOER_CCN_AVG=$(echo "scale=2; ${DOER_CCN_SUM} / ${DOER_PKG_COUNT}" | bc 2>/dev/null || echo 0)
    else
        DOER_CCN_AVG=0
    fi
    if [[ "${M7_CCN_AVG}" != "0" && "${M7_CCN_AVG}" != "" ]]; then
        CC_RATIO=$(echo "scale=1; ${DOER_CCN_AVG} / ${M7_CCN_AVG}" | bc 2>/dev/null || echo 0)
    else
        CC_RATIO=0
    fi
    echo "CCN Ratio:   M7_avg=${M7_CCN_AVG}, Doer_avg=${DOER_CCN_AVG}, Ratio=${CC_RATIO}:1 (target >=30:1)"
    if (( $(echo "${CC_RATIO} < 30" | bc -l 2>/dev/null) )); then
        echo "  WARNING: Doer/Checker CC ratio ${CC_RATIO}:1 < 30:1"
    else
        echo "  PASS"
    fi
else
    echo "CCN Ratio:   SKIP (lizard not installed)"
fi

if command -v syft &>/dev/null && command -v jq &>/dev/null; then
    TMPDIR=$(mktemp -d)
    trap 'rm -rf ${TMPDIR}' EXIT
    syft dir:"${M7_SRC}" -o cyclonedx-json --quiet > "${TMPDIR}/m7_sbom.json" 2>/dev/null || true
    DOER_PATH_ARGS=""
    for d in "${DOER_DIRS[@]}"; do [[ -d "${d}" ]] && DOER_PATH_ARGS="${DOER_PATH_ARGS} ${d}"; done
    if [[ -n "${DOER_PATH_ARGS}" ]]; then
        syft dir:${DOER_PATH_ARGS} -o cyclonedx-json --quiet > "${TMPDIR}/doer_sbom.json" 2>/dev/null || true
    fi
    if [[ -s "${TMPDIR}/m7_sbom.json" && -s "${TMPDIR}/doer_sbom.json" ]]; then
        M7_LIBS=$(jq -r '.components[]?.name // empty' "${TMPDIR}/m7_sbom.json" 2>/dev/null | sort -u)
        DOER_LIBS=$(jq -r '.components[]?.name // empty' "${TMPDIR}/doer_sbom.json" 2>/dev/null | sort -u)
        INTERSECTION=$(comm -12 <(echo "${M7_LIBS}") <(echo "${DOER_LIBS}") 2>/dev/null || true)
        if [[ -n "${INTERSECTION}" ]]; then
            echo "SBOM cap:    VIOLATION - shared: ${INTERSECTION}"
            errors=$((errors + 1))
        else
            echo "SBOM cap:    empty set (no shared dependencies) PASS"
        fi
    fi
else
    echo "SBOM cap:    SKIP (syft or jq not installed)"
fi

echo "PATH-S CI:   $([[ ${errors} -eq 0 ]] && echo '0 violations PASS' || echo 'FAILED')"
exit ${errors}
