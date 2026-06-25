#!/usr/bin/env bash
# GNC profile stack launcher (Track A A6).
#
# Brings up the full GNC integration stack for a COLREGs probe run:
#   1. The L3 sil-nodes stack (domain 42, host network) — orchestrator on 18000,
#      sil-nodes with the L3 kernel + the 3 A5 C++ adapters.
#   2. The GNC stack (docker-compose.gnc.yml) — gnc-nodes (domain 50, host net)
#      + gnc-bridge (host net, sole cross-domain process).
#
# Both stacks share the host network so the 2-context gnc_bridge can discover
# and exchange data across the two DDS domains (CRITICAL_ENV_FINDING, 2026-06-25).
#
# TASK ISOLATION (AGENTS.md): both stacks use the task-scoped compose project
# `codex-gnc-validation` and image tag `:codex-gnc-validation` so a feature
# worktree does NOT take the main stack's `mass-l3-sil` project name, ports,
# or images. Override via GNC_VALIDATION_PROJECT / GNC_BRIDGE_IMAGE env.
#
# Usage:
#   scripts/gnc-profile-start.sh            # start (idempotent up -d)
#   scripts/gnc-profile-start.sh --down     # stop + remove both stacks
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

action="${1:-up}"
export GNC_VALIDATION_PROJECT="${GNC_VALIDATION_PROJECT:-codex-gnc-validation}"
# Tag the L3 sil-nodes image task-scoped so it never overwrites main's
# mass-l3-sil-sil-nodes:latest. docker-compose.yml has no explicit image: tag,
# so compose names it {project}-{service} automatically.
export GNC_BRIDGE_IMAGE="${GNC_BRIDGE_IMAGE:-mass-l3-sil-sil-nodes:${GNC_VALIDATION_PROJECT}}"

# --- L3 sil-nodes stack (domain 42) ----------------------------------------
source scripts/local-a4000-env.sh
# TASK ISOLATION: override the project name (local-a4000-env.sh leaves it unset;
# compose would otherwise default to the dir-based mass-l3-sil).
export COMPOSE_PROJECT_NAME="${GNC_VALIDATION_PROJECT}"

case "$action" in
  up)
    echo "=== [gnc-profile] starting L3 sil-nodes stack (project=${GNC_VALIDATION_PROJECT}, domain 42, host net) ==="
    docker compose up -d --build
    echo ""
    echo "=== [gnc-profile] starting GNC stack (project=${GNC_VALIDATION_PROJECT}-gnc, domain 50, host net) ==="
    # GNC stack: separate project (suffixed -gnc) to keep gnc-nodes/gnc-bridge
    # containers distinct from the L3 services. gnc-bridge image tag is set by
    # GNC_BRIDGE_IMAGE above (task-scoped).
    COMPOSE_PROJECT_NAME="${GNC_VALIDATION_PROJECT}-gnc" \
      docker compose -f docker-compose.gnc.yml --profile gnc up -d --build
    echo ""
    echo "=== [gnc-profile] stack up. Orchestrator: ${ORCH_URL} ==="
    echo "Images: ${GNC_VALIDATION_PROJECT}-sil-nodes (L3), ${GNC_BRIDGE_IMAGE} (bridge), mass-l3-gnc:mpc_latest-20260624 (GNC)"
    echo "Run a probe: PROBE_STUCK_LIMIT=150 python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho --profile gnc"
    ;;
  --down|down)
    echo "=== [gnc-profile] stopping GNC stack (${GNC_VALIDATION_PROJECT}-gnc) ==="
    COMPOSE_PROJECT_NAME="${GNC_VALIDATION_PROJECT}-gnc" \
      docker compose -f docker-compose.gnc.yml --profile gnc down
    echo "=== [gnc-profile] stopping L3 stack (${GNC_VALIDATION_PROJECT}) ==="
    COMPOSE_PROJECT_NAME="${GNC_VALIDATION_PROJECT}" docker compose down
    ;;
  *)
    echo "Usage: $0 [up|--down]" >&2
    exit 2
    ;;
esac
