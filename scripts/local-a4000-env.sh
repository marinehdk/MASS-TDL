#!/usr/bin/env bash
# Source before local OrbStack A4000-equivalent verification.
export COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml
export ROS_DOMAIN_ID=42
export ORCH_PORT="${ORCH_PORT:-18000}"
export FOX_PORT="${FOX_PORT:-18765}"
export ORCH_URL="${ORCH_URL:-https://127.0.0.1:18000}"
export TDL_INTEGRATION_PROFILE="${TDL_INTEGRATION_PROFILE:-default}"
