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

if [[ $errors -gt 0 ]]; then
    echo ""
    echo "Doer-Checker independence: FAILED (${errors} violation(s))"
    echo "Reference: docs/Implementation/third-party-library-policy.md §3.1"
    echo "           Architecture v1.1.2 §2.5 (Decision Four) + ADR-001"
    exit 1
fi

echo "Doer-Checker independence: OK"
