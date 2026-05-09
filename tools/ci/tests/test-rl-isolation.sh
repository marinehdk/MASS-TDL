#!/usr/bin/env bash
# tools/ci/tests/test-rl-isolation.sh
# Unit tests for check-rl-isolation.sh (5 TCs per spec §v3.1.5).
# Manual run only — not gated in CI to avoid main lint pollution.

set -uo pipefail
SCRIPT="tools/ci/check-rl-isolation.sh"
PASS=0
FAIL=0

assert_eq() {
    local label="$1"; local expected="$2"; local actual="$3"
    if [[ "$expected" == "$actual" ]]; then
        echo "  ✅ PASS: $label"
        PASS=$((PASS + 1))
    else
        echo "  ❌ FAIL: $label (expected=$expected got=$actual)"
        FAIL=$((FAIL + 1))
    fi
}

cleanup() {
    rm -f src/sim_workbench/fcb_simulator/src/_test_violation.cpp
    rm -f src/sim_workbench/fcb_simulator/src/_test_violation.py
    rm -f src/l3_tdl_kernel/m1_odd_envelope_manager/_test_violation.py
}
trap cleanup EXIT

echo "== TC-1: clean tree → exit 0 =="
set +e
out=$(bash "$SCRIPT" 2>&1); rc=$?
set -e
assert_eq "TC-1 exit code" "0" "$rc"
[[ "$out" == *"PASS:"* ]] && echo "  ✅ TC-1 stdout contains PASS:" && PASS=$((PASS + 1)) || { echo "  ❌ TC-1 stdout missing PASS:"; FAIL=$((FAIL + 1)); }

echo ""
echo "== TC-2: cpp violation → exit 1 =="
cat > src/sim_workbench/fcb_simulator/src/_test_violation.cpp <<'EOF'
#include "m7_safety_supervisor/safety_supervisor.hpp"
EOF
set +e
out=$(bash "$SCRIPT" 2>&1); rc=$?
set -e
assert_eq "TC-2 exit code" "1" "$rc"
[[ "$out" == *"VIOLATION"* ]] && echo "  ✅ TC-2 stderr contains VIOLATION" && PASS=$((PASS + 1)) || { echo "  ❌ TC-2 stderr missing VIOLATION"; FAIL=$((FAIL + 1)); }
rm -f src/sim_workbench/fcb_simulator/src/_test_violation.cpp

echo ""
echo "== TC-3: cpp allowlist → exit 0 =="
cat > src/sim_workbench/fcb_simulator/src/_test_violation.cpp <<'EOF'
#include "m7_safety_supervisor/safety_supervisor.hpp"  // rl-isolation-ok: deliberate fixture for unit test
EOF
set +e
out=$(bash "$SCRIPT" 2>&1); rc=$?
set -e
assert_eq "TC-3 exit code (allowlist)" "0" "$rc"
rm -f src/sim_workbench/fcb_simulator/src/_test_violation.cpp

echo ""
echo "== TC-4: python violation in sim_workbench → exit 1 =="
cat > src/sim_workbench/fcb_simulator/src/_test_violation.py <<'EOF'
from m1_odd_envelope_manager import something
EOF
set +e
out=$(bash "$SCRIPT" 2>&1); rc=$?
set -e
assert_eq "TC-4 exit code" "1" "$rc"
rm -f src/sim_workbench/fcb_simulator/src/_test_violation.py

echo ""
echo "== TC-5: reverse direction (kernel imports sim) → exit 1 =="
cat > src/l3_tdl_kernel/m1_odd_envelope_manager/_test_violation.py <<'EOF'
import fcb_simulator
EOF
set +e
out=$(bash "$SCRIPT" 2>&1); rc=$?
set -e
assert_eq "TC-5 exit code (reverse)" "1" "$rc"
rm -f src/l3_tdl_kernel/m1_odd_envelope_manager/_test_violation.py

echo ""
echo "== Summary: PASS=$PASS FAIL=$FAIL =="
[[ $FAIL -eq 0 ]] && exit 0 || exit 1
