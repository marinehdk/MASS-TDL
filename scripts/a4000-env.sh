#!/usr/bin/env bash
# Source before `npm run sys:start` on the A4000 server.
# Remaps our ports off the shared host's jitsi(8000)/other(8765) and isolates DDS.
export COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml
export COMPOSE_PROFILES="${COMPOSE_PROFILES:-}"
export ROS_DOMAIN_ID=42
# ROS_LOCALHOST_ONLY=1 disabled: see docker-compose.a4000.yml comment.
export ORCH_PORT=18000
export FOX_PORT=18765
export ORCH_URL=https://127.0.0.1:18000
export VITE_HOST=0.0.0.0
# Default to the 2026-06-22 verified internal runtime:
# - no external plugin profile
# - orchestrator runtime profile: internal-local
# - SIL integration profile: default
#
# This preserves COLREGs route-return behavior on A4000. To start the external
# plugin integration stack, run `TDL_RUNTIME_MODE=external npm run sys:start` or
# choose external mode at the interactive prompt.
export TDL_INTEGRATION_PROFILE="${TDL_INTEGRATION_PROFILE:-default}"
export TDL_RUNTIME_PROFILE="${TDL_RUNTIME_PROFILE:-internal-local}"
