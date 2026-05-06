#!/usr/bin/env bash
# Check all .msg files follow naming rules:
#  - Field names: lower_snake_case
#  - Numeric field unit suffixes: _m / _s / _kn / _deg / _rad / _hz (warning, not error)
#  - Timestamps: stamp / received_stamp / activation_time / trigger_time / engaged_stamp
#  - Enum constant declarations: UPPER_SNAKE_CASE (skipped via "third == '='" detection)
#
# Wave 0 fixes:
#   F-CRIT-C-005: added unit-suffix heuristic check (warning level for numeric fields)
#   F-IMP-C-004: removed dead-code regex on line 21 (was not matching anything useful)
#   F-MIN-002: switched from `for f in $(find ...)` to `while read -r -d ''` (handles spaces)
#
# Required: bash >= 4.0
set -euo pipefail

if (( BASH_VERSINFO[0] < 4 )); then
    echo "FATAL: requires bash >= 4.0 (got ${BASH_VERSION})." >&2
    exit 2
fi

readonly MSG_DIRS=("src/l3_msgs/msg" "src/l3_external_msgs/msg")

# Allowed timestamp field names (architecturally-justified exceptions)
readonly TIMESTAMP_FIELDS_REGEX="^(stamp|received_stamp|activation_time|trigger_time|engaged_stamp)$"

# Allowed unit suffixes for numeric fields
readonly UNIT_SUFFIX_REGEX="(_m|_s|_kn|_deg|_rad|_hz|_pct|_count)$"

# Numeric scalar types that should ideally have unit suffixes
readonly NUMERIC_TYPES_REGEX="^(float32|float64|int8|int16|int32|int64|uint8|uint16|uint32|uint64)$"

violations=0
unit_warnings=0

# Find all .msg files (handles spaces in paths)
mapfile -d '' files < <(find "${MSG_DIRS[@]}" -name "*.msg" -print0 2>/dev/null)

for f in "${files[@]}"; do
    while IFS= read -r line; do
        # Skip comments, blank lines
        [[ "$line" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$line" ]] && continue

        # Parse type and field name
        msg_type=$(echo "$line" | awk '{print $1}')
        field=$(echo "$line" | awk '{print $2}')

        [[ -z "$field" ]] && continue

        # Skip ROS2 enum constant declarations: "uint8 FOO_BAR = 0"
        third=$(echo "$line" | awk '{print $3}')
        [[ "$third" == "=" ]] && continue

        # Skip lines where "field" position contains a "/" (it's a complex type, not a field name)
        [[ "$field" == *"/"* ]] && continue

        # Whitelist timestamp field names (per spec exceptions)
        if [[ "$field" =~ $TIMESTAMP_FIELDS_REGEX ]]; then
            continue
        fi

        # Strip array suffix from type if present (e.g. "float64[36]" -> "float64")
        base_type="${msg_type%%\[*}"

        # 1. Check lower_snake_case
        if [[ ! "$field" =~ ^[a-z][a-z0-9_]*$ ]]; then
            echo "VIOLATION: $f: field '$field' not lower_snake_case"
            violations=$((violations + 1))
            continue
        fi

        # 2. Unit suffix check (warning only — for numeric types)
        # F-CRIT-C-005: warn (not error) on numeric fields without unit suffix.
        # Many architecturally-named fields (id, count, status, mode, etc.) are exempt.
        if [[ "$base_type" =~ $NUMERIC_TYPES_REGEX ]]; then
            # Whitelisted bare names (architectural / non-physical-quantity)
            case "$field" in
                target_id|task_id|route_id|profile_id|*_id|*_idx|count|*_count) ;;
                covariance|signature|status|fallback_provided) ;;
                heading_min|heading_max|speed_min|speed_max) ;;
                *)
                    if [[ ! "$field" =~ $UNIT_SUFFIX_REGEX ]]; then
                        echo "WARN:      $f: numeric field '$field' (type ${base_type}) lacks unit suffix (_m/_s/_kn/_deg/_rad/_hz/_pct)"
                        unit_warnings=$((unit_warnings + 1))
                    fi
                    ;;
            esac
        fi
    done < "$f"
done

if [[ $violations -gt 0 ]]; then
    echo ""
    echo "Message naming: FAILED (${violations} violation(s), ${unit_warnings} warning(s))"
    exit 1
fi

if [[ $unit_warnings -gt 0 ]]; then
    echo ""
    echo "Message naming: OK with ${unit_warnings} warning(s) (unit suffix recommendations; non-blocking)"
else
    echo "Message naming: OK"
fi
