#!/usr/bin/env bash
# behavior-fix stack isolation env. Source before running scenarios in the
# colregs-behavior-fix worktree while the main mass-l3-sil stack is Up.
#
# Main stack (mass-l3-sil): host network + ROS_DOMAIN_ID=42 + ports 18000/18765.
# This stack:                       host network + ROS_DOMAIN_ID=43 + ports 18001/18766.
#
# Usage:
#   source scripts/local-behavior-fix-env.sh
#   docker compose up -d
#   python3 scripts/run_6_scenarios.py --scenario colreg-rule15-cs --restart-settle 40

export COMPOSE_PROJECT_NAME=colregs-behavior-fix
export COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml:docker-compose.behavior-fix-isolation.yml
export COMPOSE_PROFILES="${COMPOSE_PROFILES:-plugins}"

# DDS domain isolation from main stack (42).
export ROS_DOMAIN_ID=43

# Port offsets from main stack (18000/18765).
export ORCH_PORT=18001
export FOX_PORT=18766
export ORCH_URL="https://127.0.0.1:18001"

# CPU caps: this machine has 4 physical CPUs; main stack already running.
# Leave headroom for main stack.
export SIL_ORCHESTRATOR_CPUS="${SIL_ORCHESTRATOR_CPUS:-1.0}"
export SIL_NODES_CPUS="${SIL_NODES_CPUS:-3.0}"
export FOXGLOVE_CPUS="${FOXGLOVE_CPUS:-1.0}"

# Runtime profile: use internal-local (no external plugins) for COLREGs verification.
export TDL_INTEGRATION_PROFILE="${TDL_INTEGRATION_PROFILE:-default}"
export TDL_RUNTIME_PROFILE="${TDL_RUNTIME_PROFILE:-internal-local}"
