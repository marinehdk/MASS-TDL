#!/usr/bin/env bash
# Source before local OrbStack A4000-equivalent verification.
_mass_l3_repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
_mass_l3_tile_dir="${_mass_l3_repo_root}/data/tiles"
if [[ ! -f "${_mass_l3_tile_dir}/trondelag.mbtiles" && "${_mass_l3_repo_root}" == */.worktrees/* ]]; then
  _mass_l3_primary_root="${_mass_l3_repo_root%%/.worktrees/*}"
  if [[ -f "${_mass_l3_primary_root}/data/tiles/trondelag.mbtiles" ]]; then
    _mass_l3_tile_dir="${_mass_l3_primary_root}/data/tiles"
  fi
fi

export COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml
export COMPOSE_PROFILES="${COMPOSE_PROFILES:-plugins}"
export ROS_DOMAIN_ID=42
export ORCH_PORT="${ORCH_PORT:-18000}"
export FOX_PORT="${FOX_PORT:-18765}"
export MASS_L3_TILE_DIR="${MASS_L3_TILE_DIR:-${_mass_l3_tile_dir}}"
export ORCH_URL="${ORCH_URL:-https://127.0.0.1:18000}"
export TDL_INTEGRATION_PROFILE="${TDL_INTEGRATION_PROFILE:-a4000_external}"
export TDL_RUNTIME_PROFILE="${TDL_RUNTIME_PROFILE:-integration-local}"
export SIL_ORCHESTRATOR_CPUS="${SIL_ORCHESTRATOR_CPUS:-1.0}"
export SIL_NODES_CPUS="${SIL_NODES_CPUS:-4.0}"
export FOXGLOVE_CPUS="${FOXGLOVE_CPUS:-1.0}"
unset _mass_l3_repo_root _mass_l3_tile_dir _mass_l3_primary_root
