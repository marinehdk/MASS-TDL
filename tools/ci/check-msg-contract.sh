#!/usr/bin/env bash
# Verify that .msg files have required fields per ros2-idl-implementation-guide.md §1.3
# Required: builtin_interfaces/Time stamp, float32 confidence, string rationale
# Exceptions: command messages (ToRRequest, ReactiveOverrideCmd, EmergencyCommand) don't need rationale
#             pure classification messages don't need stamp/confidence/rationale
set -euo pipefail

MSG_DIR="src/l3_msgs/msg"
EXTERNAL_DIR="src/l3_external_msgs/msg"

# Messages that are pure commands (rationale not required, confidence still required)
COMMAND_MSGS="ToRRequest|ReactiveOverrideCmd|EmergencyCommand|CheckerVetoNotification"

# Messages that use non-standard timestamp field name (trigger_time instead of stamp)
TIMESTAMP_EXCEPTION_MSGS="ReactiveOverrideCmd"

# Messages that are pure data/classification (no requirement for stamp/confidence/rationale)
PURE_DATA_MSGS="EncounterClassification|ZoneConstraint|AvoidanceWaypoint|SpeedSegment|Constraint|SAT1Data|SAT2Data|SAT3Data|SATData|TimeWindow|TrackedTarget|OwnShipState|RuleActive|ASDRRecord"

errors=0

check_file() {
    local file=$1
    local basename=$(basename "$file" .msg)

    # Skip pure data messages
    if echo "$basename" | grep -qE "^(${PURE_DATA_MSGS})$"; then
        return 0
    fi

    local has_stamp=false
    local has_confidence=false
    local has_rationale=false
    local is_command=false

    if echo "$basename" | grep -qE "^(${COMMAND_MSGS})$"; then
        is_command=true
    fi

    local has_trigger_time=false

    while IFS= read -r line; do
        [[ "$line" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$line" ]] && continue
        if echo "$line" | grep -q "builtin_interfaces/Time stamp"; then has_stamp=true; fi
        if echo "$line" | grep -q "builtin_interfaces/Time trigger_time"; then has_trigger_time=true; fi
        if echo "$line" | grep -q "^float32 confidence"; then has_confidence=true; fi
        if echo "$line" | grep -q "^string rationale"; then has_rationale=true; fi
    done < "$file"

    if echo "$basename" | grep -qE "^(${TIMESTAMP_EXCEPTION_MSGS})$"; then
        has_stamp=$has_trigger_time
    fi

    if ! $has_stamp; then
        echo "VIOLATION: $file missing required field 'builtin_interfaces/Time stamp'"
        errors=$((errors + 1))
    fi
    if ! $has_confidence; then
        echo "VIOLATION: $file missing required field 'float32 confidence'"
        errors=$((errors + 1))
    fi
    if ! $is_command && ! $has_rationale; then
        echo "VIOLATION: $file missing required field 'string rationale'"
        errors=$((errors + 1))
    fi
}

for f in $(find "$MSG_DIR" "$EXTERNAL_DIR" -name "*.msg" 2>/dev/null); do
    check_file "$f"
done

if [[ $errors -gt 0 ]]; then
    echo "Message contract check: FAILED (${errors} violation(s))"
    exit 1
fi

echo "Message contract check: OK"
