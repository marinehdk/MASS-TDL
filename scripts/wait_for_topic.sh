#!/bin/bash
# wait_for_topic.sh — block until a ROS2 topic publishes ≥1 frame, or timeout.
# Usage: wait_for_topic.sh <topic_name> [timeout_s] [--hz-window N]
# Exit: 0 = topic alive and publishing, 1 = timeout expired

set -euo pipefail

TOPIC="${1:?Usage: wait_for_topic.sh <topic_name> [timeout_s]}"
TIMEOUT="${2:-30}"
HZ_WINDOW="${3:-3}"

# Source ROS2 environment — caller must ensure /opt/ros/humble/setup.bash
# and /opt/ws/install/setup.bash are already sourced, or pass --source.

echo "[wait_for_topic] $(date -u +'%Y-%m-%dT%H:%M:%SZ') Waiting for '$TOPIC' (timeout=${TIMEOUT}s)..."

ELAPSED=0
INTERVAL=2  # seconds between probes

while [ "$ELAPSED" -lt "$TIMEOUT" ]; do
    # ros2 topic hz with --window gives average rate; if it returns non-zero
    # exit AND prints "average rate", the topic is publishing.
    HZ_OUTPUT=$(ros2 topic hz "$TOPIC" --window "$HZ_WINDOW" 2>&1) || true
    if echo "$HZ_OUTPUT" | grep -q 'average rate'; then
        AVG_RATE=$(echo "$HZ_OUTPUT" | grep 'average rate' | tail -1 | awk '{print $3}')
        echo "[wait_for_topic] $(date -u +'%Y-%m-%dT%H:%M:%SZ') '$TOPIC' publishing @ ${AVG_RATE} Hz (after ${ELAPSED}s)"
        exit 0
    fi

    echo "[wait_for_topic] ... '$TOPIC' not yet publishing (elapsed=${ELAPSED}s, will retry in ${INTERVAL}s)"
    sleep "$INTERVAL"
    ELAPSED=$((ELAPSED + INTERVAL))
done

echo "[wait_for_topic] $(date -u +'%Y-%m-%dT%H:%M:%SZ') TIMEOUT: '$TOPIC' not publishing after ${TIMEOUT}s" >&2

# Diagnostic dump on failure
echo "[DIAGNOSTIC] Active nodes:" >&2
ros2 node list 2>&1 >&2 || true
echo "[DIAGNOSTIC] Active topics:" >&2
ros2 topic list 2>&1 >&2 || true
exit 1
