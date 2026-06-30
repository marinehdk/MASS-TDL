#!/usr/bin/env bash
# Doer-Checker Independence Verification (ADR-001 + Decision Four)
#
# Verifies that M7 Safety Supervisor does NOT depend on M1–M6 internal headers
# nor forbidden third-party libraries. Per docs/Implementation/third-party-library-policy.md §3.1
# independence matrix.
#
# Wave 0 fixes:
#   F-CRIT-C-001: fail loudly if M7 source directory is missing (not silent PASS)
#   F-CRIT-C-002: added Boost.PropertyTree and IPOPT case variants to forbidden lists
#   F-IMP-C-001: docstring clarification: comments containing #include also flagged
#                (PATH-S白盒规则严，不区分注释)
#
# Required: bash >= 4.0
set -euo pipefail

if (( BASH_VERSINFO[0] < 4 )); then
    echo "FATAL: requires bash >= 4.0 (got ${BASH_VERSION}). On macOS: 'brew install bash'." >&2
    exit 2
fi

readonly M7_SRC="src/l3_tdl_kernel/m7_safety_supervisor"

# F-CRIT-C-001: fail if M7 source directory missing — never silent PASS
if [[ ! -d "${M7_SRC}" ]]; then
    echo "FATAL: M7 source directory '${M7_SRC}' not found." >&2
    echo "       Doer-Checker independence cannot be verified — this means either" >&2
    echo "       (a) M7 package not yet created (Wave 1 startup), or" >&2
    echo "       (b) repo was checked out incorrectly. Investigate before retrying." >&2
    exit 2
fi

errors=0

# Forbidden internal M1–M6 headers (M7 may only see l3_msgs/msg/* + ROS2 stdlib)
readonly FORBIDDEN_INTERNAL=(
    "mass_l3/m1/"
    "mass_l3/m2/"
    "mass_l3/m3/"
    "mass_l3/m4/"
    "mass_l3/m5/"
    "mass_l3/m6/"
)

# M8 isolation (D3.3a spec 7.3) - F-CRIT-C-003
readonly FORBIDDEN_INTERNAL_M8=("mass_l3/m8/")

# Slice K allowlist: l3_risk_model is a deterministic geometry/risk utility, not a
# doer reasoning implementation. M7 may link/include it only for X-axis checker
# calculations; the VETO path must not share M1-M6 internal module headers.
readonly ALLOWED_SHARED_LIBS=("l3_risk_model")

for lib in "${ALLOWED_SHARED_LIBS[@]}"; do
    if grep -rn "${lib}" "${M7_SRC}" 2>/dev/null; then
        echo "ALLOWLIST: M7 references ${lib} (deterministic geometry library; no doer reasoning path shared)"
    fi
done

for h in "${FORBIDDEN_INTERNAL_M8[@]}"; do
    if grep -rn "#include.*${h}" "${M7_SRC}" 2>/dev/null; then
        echo "VIOLATION: M7 includes forbidden M8 internal header pattern '${h}'"
        errors=$((errors + 1))
    fi
done

# Forbidden 3rd-party libraries (per third-party-library-policy.md §3.1 independence matrix)
# Includes case variants for IPOPT (commonly seen as `coin/IpoptApplication.hpp`)
readonly FORBIDDEN_3RDPARTY=(
    "casadi/"
    "Ipopt"          # Matches IpoptApplication.hpp etc. (case-sensitive grep)
    "coin/Ipopt"     # IPOPT alternative include path
    "GeographicLib/"
    "nlohmann/json"
    "nlohmann_json"
    "boost/geometry"
    "boost/property_tree"   # F-CRIT-C-002: M7 must not parse complex configs
)

# Forbidden link library names (CMake target_link_libraries patterns)
readonly FORBIDDEN_LINKS=(
    "casadi"
    "Ipopt"
    "ipopt"
    "GeographicLib"
    "geographiclib"
    "nlohmann_json"
    "boost_geometry"
    "boost_property_tree"   # F-CRIT-C-002
)

# 1. Check #include statements (excludes ANY comment is a TODO; for now strict)
for h in "${FORBIDDEN_INTERNAL[@]}"; do
    if grep -rn "#include.*${h}" "${M7_SRC}" 2>/dev/null; then
        echo "VIOLATION: M7 includes forbidden internal header pattern '${h}'"
        errors=$((errors + 1))
    fi
done

for h in "${FORBIDDEN_3RDPARTY[@]}"; do
    if grep -rn "#include.*${h}" "${M7_SRC}" 2>/dev/null; then
        echo "VIOLATION: M7 includes forbidden third-party header pattern '${h}'"
        errors=$((errors + 1))
    fi
done

# 2. Check CMakeLists.txt target_link_libraries for forbidden libraries
if [[ -f "${M7_SRC}/CMakeLists.txt" ]]; then
    for lib in "${FORBIDDEN_LINKS[@]}"; do
        if grep -E "target_link_libraries\b.*\b${lib}\b" "${M7_SRC}/CMakeLists.txt" 2>/dev/null; then
            echo "VIOLATION: M7 CMakeLists.txt links forbidden library '${lib}'"
            errors=$((errors + 1))
        fi
    done

    # 3. Check find_package for forbidden libraries
    for lib in "${FORBIDDEN_LINKS[@]}"; do
        if grep -E "find_package\s*\(\s*${lib}\b" "${M7_SRC}/CMakeLists.txt" 2>/dev/null; then
            echo "VIOLATION: M7 CMakeLists.txt find_package() for forbidden library '${lib}'"
            errors=$((errors + 1))
        fi
    done
else
    echo "WARNING: ${M7_SRC}/CMakeLists.txt not found (Wave 1 starts soon — auto-fail when expected)"
fi

# 4. Check package.xml for forbidden depend tags
if [[ -f "${M7_SRC}/package.xml" ]]; then
    for lib in "${FORBIDDEN_LINKS[@]}"; do
        if grep -E "<(build_depend|exec_depend|depend)>\s*${lib}\b" "${M7_SRC}/package.xml" 2>/dev/null; then
            echo "VIOLATION: M7 package.xml declares dependency on forbidden library '${lib}'"
            errors=$((errors + 1))
        fi
    done
fi

# ============================================================
# 5. LOC Ratio Check (ARCH §11.x: M7:Doer ≥ 100:1)
#    Uses cloc to count lines of code across M7 and M1-M6.
# ============================================================
readonly DOER_DIRS=(
    "src/l3_tdl_kernel/m1_odd_envelope_manager"
    "src/l3_tdl_kernel/m2_world_model"
    "src/l3_tdl_kernel/m3_mission_manager"
    "src/l3_tdl_kernel/m4_behavior_arbiter"
    "src/l3_tdl_kernel/m5_tactical_planner"
    "src/l3_tdl_kernel/m6_colregs_reasoner"
)

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
    echo "PATH-S LOC: M7=${M7_LOC} lines, Doers(M1-M6)=${DOER_LOC} lines, Ratio=${LOC_RATIO}:1 (target ≥100:1)"
    if [[ ${LOC_RATIO} -lt 100 ]]; then
        echo "VIOLATION: Doer/Checker LOC ratio ${LOC_RATIO}:1 < 100:1 threshold (ARCH §11.x)"
        errors=$((errors + 1))
    fi
else
    echo "WARNING: cloc not installed — skipping LOC ratio check"
fi

# ============================================================
# 6. Cyclomatic Complexity Ratio (ARCH §11.x: Doer_avg / M7_avg ≥ 30:1)
#    Uses lizard to compute average CC per function.
# ============================================================
if command -v lizard &>/dev/null; then
    # lizard summary line format: "Total nloc   Avg.NLOC  AvgCCN  Avg.token  Fun Cnt  Warning cnt  Fun Rt  nloc Rt"
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
    echo "PATH-S CCN: M7_avg=${M7_CCN_AVG}, Doer_avg=${DOER_CCN_AVG}, Ratio=${CC_RATIO}:1 (target ≥30:1)"
    if (( $(echo "${CC_RATIO} < 30" | bc -l 2>/dev/null) )); then
        echo "VIOLATION: Doer/Checker CC ratio ${CC_RATIO}:1 < 30:1 threshold (ARCH §11.x)"
        errors=$((errors + 1))
    fi
else
    echo "WARNING: lizard not installed — skipping cyclomatic complexity ratio check"
fi

# ============================================================
# 7. SBOM Intersection Check (ARCH §11.x: M7 ∩ Doer deps = ∅)
#    Uses syft to generate CycloneDX SBOM and checks for shared components.
# ============================================================
if command -v syft &>/dev/null; then
    TMPDIR=$(mktemp -d)
    trap 'rm -rf ${TMPDIR}' EXIT

    # Generate SBOM for M7
    syft dir:"${M7_SRC}" -o cyclonedx-json --quiet > "${TMPDIR}/m7_sbom.json" 2>/dev/null || true

    # Generate merged SBOM for Doers
    DOER_PATH_ARGS=""
    for d in "${DOER_DIRS[@]}"; do
        [[ -d "${d}" ]] && DOER_PATH_ARGS="${DOER_PATH_ARGS} ${d}"
    done
    if [[ -n "${DOER_PATH_ARGS}" ]]; then
        syft dir:${DOER_PATH_ARGS} -o cyclonedx-json --quiet > "${TMPDIR}/doer_sbom.json" 2>/dev/null || true
    fi

    # Extract library names from both SBOMs and check intersection
    if [[ -s "${TMPDIR}/m7_sbom.json" && -s "${TMPDIR}/doer_sbom.json" ]]; then
        M7_LIBS=$(jq -r '.components[]?.name // empty' "${TMPDIR}/m7_sbom.json" 2>/dev/null | sort -u)
        DOER_LIBS=$(jq -r '.components[]?.name // empty' "${TMPDIR}/doer_sbom.json" 2>/dev/null | sort -u)
        INTERSECTION=$(comm -12 <(echo "${M7_LIBS}") <(echo "${DOER_LIBS}") 2>/dev/null || true)
        if [[ -n "${INTERSECTION}" ]]; then
            echo "VIOLATION: Shared SBOM components between M7 and Doers:"
            echo "${INTERSECTION}" | while read -r lib; do echo "  - ${lib}"; done
            errors=$((errors + 1))
        else
            echo "SBOM intersection: ∅ (M7 and Doers share no dependencies)"
        fi
    else
        echo "WARNING: SBOM generation incomplete — skipping intersection check"
    fi
else
    echo "INFO: syft not installed — skipping SBOM intersection check (non-blocking)"
fi

if [[ $errors -gt 0 ]]; then
    echo ""
    echo "Doer-Checker independence: FAILED (${errors} violation(s))"
    echo "Reference: docs/Implementation/third-party-library-policy.md §3.1"
    echo "           Architecture v1.1.2 §2.5 (Decision Four) + ADR-001"
    exit 1
fi

echo "Doer-Checker independence: OK"
