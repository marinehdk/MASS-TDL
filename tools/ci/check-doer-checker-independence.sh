#!/usr/bin/env bash
# checks that M7 does not depend on M1–M6 internal headers or
# forbidden third-party libraries (ADR-001 + Decision Four)
set -euo pipefail

M7_SRC="src/m7_safety_supervisor"
errors=0

forbidden_internal=("mass_l3/m1/" "mass_l3/m2/" "mass_l3/m3/" "mass_l3/m4/" "mass_l3/m5/" "mass_l3/m6/")
forbidden_3rdparty=("casadi/" "Ipopt" "GeographicLib/" "nlohmann/json" "boost/geometry")

for h in "${forbidden_internal[@]}"; do
    if grep -rn "#include.*${h}" "${M7_SRC}" 2>/dev/null; then
        echo "VIOLATION: M7 includes forbidden internal header '${h}'"
        errors=$((errors + 1))
    fi
done

for h in "${forbidden_3rdparty[@]}"; do
    if grep -rn "#include.*${h}" "${M7_SRC}" 2>/dev/null; then
        echo "VIOLATION: M7 includes forbidden third-party header '${h}'"
        errors=$((errors + 1))
    fi
done

# Check CMakeLists.txt for forbidden library links
forbidden_links=("casadi" "Ipopt" "GeographicLib" "nlohmann_json" "boost_geometry")
for lib in "${forbidden_links[@]}"; do
    if grep -E "target_link_libraries.*${lib}" "${M7_SRC}/CMakeLists.txt" 2>/dev/null; then
        echo "VIOLATION: M7 links forbidden library '${lib}'"
        errors=$((errors + 1))
    fi
done

if [[ $errors -gt 0 ]]; then
    echo "Doer-Checker independence: FAILED (${errors} violation(s))"
    exit 1
fi

echo "Doer-Checker independence: OK"
