#!/usr/bin/env bash
# Verify v1.1.2 §15.1 IDL completeness: all required .msg files exist, AND
# verify per-message required fields per ros2-idl-implementation-guide.md §1.3.
#
# Wave 0 fixes:
#   F-CRIT-C-003: added §15.1 IDL completeness check (was missing — only field check existed)
#   F-IMP-C-002: replaced 4-fork-per-line `echo|grep` pattern with single `grep -F`
#                (faster: 4 forks/line × 30 lines × 50 msgs ~ 6000 forks → 50 reads)
#
# Required: bash >= 4.0
set -euo pipefail

if (( BASH_VERSINFO[0] < 4 )); then
    echo "FATAL: requires bash >= 4.0 (got ${BASH_VERSION})." >&2
    exit 2
fi

readonly MSG_DIR="src/l3_msgs/msg"
readonly EXTERNAL_DIR="src/l3_external_msgs/msg"

errors=0

# ===============================================================
# Part A: §15.1 IDL completeness — required .msg file roster
# Source of truth: docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md §15.1
# ===============================================================

# 14 main L3 messages (l3_msgs/)
readonly L3_MSGS_MAIN=(
    "ODDState"
    "ModeCmd"
    "WorldState"
    "MissionGoal"
    "RouteReplanRequest"
    "BehaviorPlan"
    "COLREGsConstraint"
    "AvoidancePlan"
    "ReactiveOverrideCmd"
    "SafetyAlert"
    "ASDRRecord"
    "ToRRequest"
    "UIState"
    "SATData"
)

# 11 shared sub-types (l3_msgs/) — TimeWindow moved to l3_external_msgs (Wave 0 F-CRIT-B-003)
readonly L3_MSGS_SUBTYPES=(
    "TrackedTarget"
    "OwnShipState"
    "EncounterClassification"
    "ZoneConstraint"
    "AvoidanceWaypoint"
    "SpeedSegment"
    "Constraint"
    "RuleActive"
    "SAT1Data"
    "SAT2Data"
    "SAT3Data"
)

# 11 cross-layer messages (l3_external_msgs/) + TimeWindow (moved Wave 0)
readonly L3_EXTERNAL_MSGS=(
    "VoyageTask"
    "PlannedRoute"
    "SpeedProfile"
    "ReplanResponse"
    "TrackedTargetArray"
    "FilteredOwnShipState"
    "EnvironmentState"
    "CheckerVetoNotification"
    "EmergencyCommand"
    "ReflexActivationNotification"
    "OverrideActiveSignal"
    "TimeWindow"
)

echo "===== Part A: §15.1 IDL Completeness ====="
for msg in "${L3_MSGS_MAIN[@]}" "${L3_MSGS_SUBTYPES[@]}"; do
    if [[ ! -f "${MSG_DIR}/${msg}.msg" ]]; then
        echo "MISSING: l3_msgs/${msg}.msg (per architecture v1.1.2 §15.1)"
        errors=$((errors + 1))
    fi
done

for msg in "${L3_EXTERNAL_MSGS[@]}"; do
    if [[ ! -f "${EXTERNAL_DIR}/${msg}.msg" ]]; then
        echo "MISSING: l3_external_msgs/${msg}.msg (per architecture v1.1.2 §15.1)"
        errors=$((errors + 1))
    fi
done

if [[ $errors -gt 0 ]]; then
    echo "Part A FAILED: ${errors} required .msg files missing"
fi

# ===============================================================
# Part B: per-message required fields
# Spec: ros2-idl-implementation-guide.md §1.3
#   - Required: builtin_interfaces/Time stamp, float32 confidence, string rationale
#   - Command exception (no rationale): ToRRequest, ReactiveOverrideCmd, EmergencyCommand,
#                                       CheckerVetoNotification, ReflexActivationNotification
#   - Pure-data exception (no required fields): TrackedTarget, OwnShipState, EncounterClassification,
#                                                ZoneConstraint, AvoidanceWaypoint, SpeedSegment,
#                                                Constraint, RuleActive, SAT*Data, TimeWindow,
#                                                ASDRRecord, OverrideActiveSignal
#   - Non-standard timestamp (trigger_time / activation_time): ReactiveOverrideCmd,
#                                                                EmergencyCommand,
#                                                                ReflexActivationNotification
# ===============================================================

readonly COMMAND_MSGS_REGEX="^(ToRRequest|ReactiveOverrideCmd|EmergencyCommand|CheckerVetoNotification|ReflexActivationNotification)$"
readonly TIMESTAMP_TRIGGER_REGEX="^(ReactiveOverrideCmd|EmergencyCommand)$"
readonly TIMESTAMP_ACTIVATION_REGEX="^(ReflexActivationNotification)$"
readonly PURE_DATA_REGEX="^(TrackedTarget|OwnShipState|EncounterClassification|ZoneConstraint|AvoidanceWaypoint|SpeedSegment|Constraint|RuleActive|SAT1Data|SAT2Data|SAT3Data|TimeWindow|ASDRRecord|OverrideActiveSignal)$"

field_errors=0

check_file() {
    local file=$1
    local basename
    basename=$(basename "$file" .msg)

    # Skip pure data messages
    if [[ "$basename" =~ $PURE_DATA_REGEX ]]; then
        return 0
    fi

    # Read full content once (F-IMP-C-002: 1 read instead of N forks)
    local content
    content=$(<"$file")

    local has_stamp=false has_confidence=false has_rationale=false
    local is_command=false

    if [[ "$basename" =~ $COMMAND_MSGS_REGEX ]]; then
        is_command=true
    fi

    # Use grep -F for fixed-string match (no fork loop)
    if grep -qF "builtin_interfaces/Time stamp" <<< "$content"; then has_stamp=true; fi
    if grep -qE "^float32 confidence" <<< "$content"; then has_confidence=true; fi
    if grep -qE "^string rationale" <<< "$content"; then has_rationale=true; fi

    # Timestamp exception: trigger_time replaces stamp
    if [[ "$basename" =~ $TIMESTAMP_TRIGGER_REGEX ]]; then
        if grep -qF "builtin_interfaces/Time trigger_time" <<< "$content"; then
            has_stamp=true
        fi
    fi
    # Timestamp exception: activation_time replaces stamp (ReflexActivationNotification)
    if [[ "$basename" =~ $TIMESTAMP_ACTIVATION_REGEX ]]; then
        if grep -qF "builtin_interfaces/Time activation_time" <<< "$content"; then
            has_stamp=true
        fi
    fi

    if ! $has_stamp; then
        echo "VIOLATION: $file missing required timestamp field (stamp/trigger_time/activation_time)"
        field_errors=$((field_errors + 1))
    fi

    # ReflexActivationNotification spec doesn't require confidence/rationale (pure event signal)
    if [[ "$basename" == "ReflexActivationNotification" || "$basename" == "OverrideActiveSignal" ]]; then
        return 0
    fi

    # EmergencyCommand spec excludes rationale (command), still requires confidence
    if ! $has_confidence; then
        echo "VIOLATION: $file missing required field 'float32 confidence'"
        field_errors=$((field_errors + 1))
    fi

    if ! $is_command && ! $has_rationale; then
        echo "VIOLATION: $file missing required field 'string rationale'"
        field_errors=$((field_errors + 1))
    fi
}

echo ""
echo "===== Part B: Per-Message Required Fields ====="
mapfile -d '' all_msgs < <(find "$MSG_DIR" "$EXTERNAL_DIR" -name "*.msg" -print0 2>/dev/null)
for f in "${all_msgs[@]}"; do
    check_file "$f"
done

errors=$((errors + field_errors))

if [[ $errors -gt 0 ]]; then
    echo ""
    echo "Message contract check: FAILED (${errors} total violation(s))"
    echo "Reference: ros2-idl-implementation-guide.md §1.3 + architecture v1.1.2 §15.1"
    exit 1
fi

echo ""
echo "Message contract check: OK"
echo "  - §15.1 IDL completeness: $(( ${#L3_MSGS_MAIN[@]} + ${#L3_MSGS_SUBTYPES[@]} + ${#L3_EXTERNAL_MSGS[@]} )) messages present"
echo "  - Per-message required fields: all conform"
