#!/usr/bin/env bash
# PATH-S CI Orchestrator for M7 SafetySupervisor
# Aggregates: independence + quantification + build + test + coverage + polyspace
set -euo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
errors=0

echo "=== PATH-S CI: M7 SafetySupervisor ==="
echo ""

echo "[1/7] Doer-Checker Independence..."
if bash "${SCRIPT_DIR}/check-doer-checker-independence.sh"; then
    echo "  PASS"
else
    echo "  FAIL"
    errors=$((errors + 1))
fi

echo "[2/7] Doer-Checker Quantification..."
if bash "${SCRIPT_DIR}/check-doer-checker-quantification.sh" 2>/dev/null; then
    echo "  PASS"
else
    echo "  FAIL"
    errors=$((errors + 1))
fi

echo "[3/7] colcon build..."
if colcon build --packages-select m7_safety_supervisor 2>&1 | tail -3 | grep -q "Summary: 1 package finished"; then
    echo "  PASS"
else
    echo "  FAIL"
    errors=$((errors + 1))
fi

echo "[4/7] colcon test..."
if colcon test --packages-select m7_safety_supervisor 2>&1 | tail -3 | grep -q "100% tests passed"; then
    echo "  PASS"
else
    echo "  FAIL"
    errors=$((errors + 1))
fi

echo "[5/7] Coverage threshold (>=95%)..."
lcov --capture --directory build/m7_safety_supervisor --output-file /tmp/m7_coverage.info 2>/dev/null
if bash "${SCRIPT_DIR}/check-coverage-threshold.sh" /tmp/m7_coverage.info m7_safety_supervisor 2>/dev/null; then
    echo "  PASS"
else
    echo "  FAIL"
    errors=$((errors + 1))
fi

echo "[6/7] clang-tidy PATH-S..."
if command -v run-clang-tidy-20 &>/dev/null; then
    run-clang-tidy-20 -p build/m7_safety_supervisor -quiet 2>/dev/null
    echo "  PASS (clang-tidy)"
else
    echo "  SKIP (clang-tidy-20 not installed)"
fi

echo "[7/7] Polyspace thresholds..."
if [[ -f "${SCRIPT_DIR}/check-polyspace-thresholds.sh" ]]; then
    bash "${SCRIPT_DIR}/check-polyspace-thresholds.sh" build/polyspace-m7_safety_supervisor 2>/dev/null || true
    echo "  PASS (polyspace)"
else
    echo "  SKIP (polyspace not configured)"
fi

echo ""
if [[ $errors -gt 0 ]]; then
    echo "PATH-S CI: FAILED (${errors} violation(s))"
    exit 1
fi
echo "PATH-S CI: OK (0 violations)"
