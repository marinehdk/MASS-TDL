#!/bin/bash
# tools/ci/check-m7-fmi-isolation.sh
# Enforce: M7 Safety Supervisor NEVER imports/links libcosim or fmi_bridge.
# Architecture v1.1.3-pre-stub §F.2: M7 KPI < 10ms, ROS2 native only.

set -euo pipefail
errors=0
M7_DIR="src/l3_tdl_kernel/m7_safety_supervisor"

echo "=== M7-FMI Isolation Check ==="

echo -n "  Checking C++ includes in M7... "
if grep -rEn '#include.*(cosim|fmi_bridge)' "$M7_DIR" \
    --include='*.hpp' --include='*.cpp' --include='*.h' 2>/dev/null; then
    echo "FAIL: M7 includes libcosim/fmi_bridge header"
    errors=$((errors + 1))
else
    echo "OK"
fi

echo -n "  Checking CMake deps in M7... "
if grep -rEn '(libcosim|fmi_bridge)' "$M7_DIR/CMakeLists.txt" 2>/dev/null; then
    echo "FAIL: M7 CMakeLists.txt references libcosim/fmi_bridge"
    errors=$((errors + 1))
else
    echo "OK"
fi

echo -n "  Checking Python imports in M7... "
if grep -rEn 'import.*(cosim|fmi_bridge|libcosim)' "$M7_DIR" \
    --include='*.py' 2>/dev/null; then
    echo "FAIL: M7 Python code imports libcosim/fmi_bridge"
    errors=$((errors + 1))
else
    echo "OK"
fi

if [[ $errors -gt 0 ]]; then
    echo ""
    echo "M7-FMI isolation VIOLATED: $errors error(s)." >&2
    echo "Architecture §F.2: M7 Safety Supervisor is ROS2 native, never crosses FMI boundary." >&2
    exit 1
fi

echo "PASS: M7-FMI isolation check clean."
exit 0
