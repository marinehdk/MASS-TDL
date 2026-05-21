#!/bin/bash
# PR-trigger Smoke 10: schema validation on 10 key scenarios
# Part of D1.6 CI fast gate. Exit 0 = all pass, Exit 1 = any fail.
#
# Updated 2026-05-21: Use correct scenario paths (IMAZU标准测试 + smoke/).
set -euo pipefail

PYTHON_VALIDATOR="tools/sil/cerberus_validator.py"

# Smoke 10 scenario list — 10 diverse scenarios for coverage cube ≥10 cells
# Sources: IMAZU标准测试 (4 selected) + smoke/ (6 newly authored)
SMOKE_SCENARIOS=(
    "scenarios/IMAZU标准测试/imazu-01-ho.yaml"
    "scenarios/IMAZU标准测试/imazu-02-cr-gw.yaml"
    "scenarios/IMAZU标准测试/imazu-03-ot.yaml"
    "scenarios/IMAZU标准测试/imazu-08-ms.yaml"
    "scenarios/smoke/smoke-04-ho-open-bf4.yaml"
    "scenarios/smoke/smoke-05-cr-coastal-calm.yaml"
    "scenarios/smoke/smoke-06-ho-port-calm.yaml"
    "scenarios/smoke/smoke-07-ot-offshore-bf2.yaml"
    "scenarios/smoke/smoke-08-ho-open-degraded.yaml"
    "scenarios/smoke/smoke-09-cr-open-bf6.yaml"
)

echo "=== D1.6 Smoke 10: Schema Validation ==="
echo "Validator: $PYTHON_VALIDATOR"
echo "Scenarios: ${#SMOKE_SCENARIOS[@]}"
echo ""

FAIL_COUNT=0
PASS_COUNT=0
SKIP_COUNT=0

for yaml_file in "${SMOKE_SCENARIOS[@]}"; do
    scenario_name="$(basename "$yaml_file" .yaml)"
    if [ ! -f "$yaml_file" ]; then
        echo "SKIP: $scenario_name — file not found: $yaml_file"
        SKIP_COUNT=$((SKIP_COUNT + 1))
        continue
    fi

    if python3 -c "
import sys; sys.path.insert(0, 'tools/sil')
from cerberus_validator import validate_file
from pathlib import Path
validate_file(Path('$yaml_file'))
print('PASS')
" 2>/dev/null; then
        echo "PASS: $scenario_name"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo "FAIL: $scenario_name — cerberus validation failed"
        # Show validation errors for debugging
        python3 -c "
import sys; sys.path.insert(0, 'tools/sil')
from cerberus_validator import validate_file
from pathlib import Path
try:
    validate_file(Path('$yaml_file'))
except Exception as e:
    print(f'  Error: {e}', file=sys.stderr)
" 2>&1 || true
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

TOTAL=$((PASS_COUNT + FAIL_COUNT))
echo ""
echo "=== Results: $PASS_COUNT/$TOTAL passed, $FAIL_COUNT failed, $SKIP_COUNT skipped ==="

# Output summary to file for CI artifacts
cat > smoke10_results.txt << EOF
Smoke 10 Schema Validation
Date: $(date -u +%Y-%m-%dT%H:%M:%SZ)
Passed: $PASS_COUNT
Failed: $FAIL_COUNT
Skipped: $SKIP_COUNT
Total: $((TOTAL + SKIP_COUNT))
Status: $([ "$FAIL_COUNT" -gt 0 ] && echo "FAILED" || echo "PASSED")
EOF

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "SMOKE 10 FAILED"
    exit 1
elif [ "$TOTAL" -eq 0 ]; then
    echo "SMOKE 10 SKIPPED (no scenario files found)"
    exit 0
else
    echo "SMOKE 10 PASSED"
    exit 0
fi
