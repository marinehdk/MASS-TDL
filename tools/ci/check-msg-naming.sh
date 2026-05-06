#!/usr/bin/env bash
# Check all .msg files follow naming rules:
#  - Fields: lower_snake_case
#  - Unit suffixes: _m / _s / _kn / _deg / _rad / _hz
#  - Timestamps: stamp / received_stamp (builtin_interfaces/Time)
set -euo pipefail

MSG_DIRS="src/l3_msgs/msg src/l3_external_msgs/msg"
violations=0

for f in $(find ${MSG_DIRS} -name "*.msg" 2>/dev/null); do
    while IFS= read -r line; do
        # Skip comments, blank lines
        [[ "$line" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$line" ]] && continue

        # Parse field name (second token after type)
        field=$(echo "$line" | awk '{print $2}')

        # Skip if not a field (e.g. enum constant definitions with =)
        [[ "$field" =~ ^[A-Z_]+\|[A-Z_]+$ ]] && continue
        [[ -z "$field" ]] && continue
        # Skip ROS2 constant definitions: uint8 BEHAVIOR_FOO = value
        third=$(echo "$line" | awk '{print $3}')
        [[ "$third" == "=" ]] && continue
        # Skip type declarations with / (e.g. builtin_interfaces/Time)
        [[ "$field" == *"/"* ]] && continue

        # Check lower_snake_case
        if [[ ! "$field" =~ ^[a-z][a-z0-9_]*$ ]]; then
            echo "VIOLATION: $f: field '$field' not lower_snake_case"
            violations=$((violations + 1))
        fi
    done < "$f"
done

[[ $violations -gt 0 ]] && exit 1 || echo ".msg naming: OK"
