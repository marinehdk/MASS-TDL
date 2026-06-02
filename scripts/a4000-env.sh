#!/usr/bin/env bash
# Source before `npm run sys:start` on the A4000 server.
# Remaps our ports off the shared host's jitsi(8000)/other(8765) and isolates DDS.
export COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml
export ROS_DOMAIN_ID=42
# ROS_LOCALHOST_ONLY=1 disabled: see docker-compose.a4000.yml comment.
export ORCH_PORT=18000
export FOX_PORT=18765
export ORCH_URL=https://127.0.0.1:18000
export VITE_HOST=0.0.0.0
