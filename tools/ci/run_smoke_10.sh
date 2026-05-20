#!/bin/bash
# PR-trigger Smoke 10: schema validation on 10 Imazu key scenarios
# Part of D1.6 CI fast gate. Exit 0 = all pass, Exit 1 = any fail.
set -euo pipefail

SCHEMA="tools/sil/cerberus_schema/fcb_scenario_v2.yaml"
SCENARIO_DIR="scenarios/imazu22"
PYTHON_VALIDATOR="tools/sil/cerberus_validator.py"

# Smoke 10 scenario list (see 02-scenario-schema.md §6.2)
SMOKE_SCENARIOS=(
    "imazu-01-ho-v1.0"
    "imazu-02-cr-gw-v1.0"
    "imazu-03-ot-v1.0"
    "imazu-04-cr-so-v1.0"
    "imazu-05-ms-v1.0"
    "imazu-08-ms-v1.0"
    "imazu-12-ms-v1.0"
    "imazu-16-ms-v1.0"
    "imazu-18-ms-v1.0"
    "imazu-22-ms-v1.0"
)

echo "=== D1.6 Smoke 10: Schema Validation ==="
echo "Schema: $SCHEMA"
echo "Scenarios: ${#SMOKE_SCENARIOS[@]}"
echo ""

FAIL_COUNT=0
PASS_COUNT=0

for scenario_id in "${SMOKE_SCENARIOS[@]}"; do
    yaml_file="${SCENARIO_DIR}/${scenario_id}.yaml"
    if [ ! -f "$yaml_file" ]; then
        echo "SKIP: $scenario_id — file not found: $yaml_file (may need D1.3.2.1 Task C)"
        continue
    fi

    if python -c "
import sys; sys.path.insert(0, 'tools/sil')
from cerberus_validator import validate_yaml
import yaml
data = yaml.safe_load(open('$yaml_file'))
validate_yaml(data)
print('PASS')
" 2>/dev/null; then
        echo "PASS: $scenario_id"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo "FAIL: $scenario_id — cerberus validation failed"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

TOTAL=$((PASS_COUNT + FAIL_COUNT))
echo ""
echo "=== Results: $PASS_COUNT/$TOTAL passed, $FAIL_COUNT failed ==="

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "SMOKE 10 FAILED"
    exit 1
elif [ "$TOTAL" -eq 0 ]; then
    echo "SMOKE 10 SKIPPED (no scenario files found — may need D1.3.2.1 Task C)"
    exit 0
else
    echo "SMOKE 10 PASSED"
    exit 0
fi
