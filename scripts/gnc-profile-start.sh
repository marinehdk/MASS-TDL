#!/usr/bin/env bash
# GNC profile stack launcher (Track A A6).
#
# Brings up the full GNC integration stack for a COLREGs probe run:
#   1. The L3 sil-nodes stack (domain 42, host network) — orchestrator on 18000,
#      sil-nodes with the L3 kernel + the 3 A5 C++ adapters + gnc_bridge.
#   2. The GNC stack (docker-compose.gnc.yml) — gnc-nodes (domain 50, host net)
#      + gnc-bridge (host net, sole cross-domain process).
#
# Both stacks share the host network so the 2-context gnc_bridge can discover
# and exchange data across the two DDS domains (CRITICAL_ENV_FINDING, 2026-06-25).
#
# Usage:
#   scripts/gnc-profile-start.sh            # start (idempotent up -d)
#   scripts/gnc-profile-start.sh --down     # stop + remove the GNC stack only
#                                           # (L3 stack left for mass-l3-sil stop)
#
# The L3 stack uses the project name from the environment (default mass-l3-sil);
# the GNC stack uses its own compose project name `codex-gnc` (set in
# docker-compose.gnc.yml). They do NOT collide because gnc-nodes/gnc-bridge are
# in the codex-gnc project.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

action="${1:-up}"

# --- L3 sil-nodes stack (domain 42) ----------------------------------------
source scripts/local-a4000-env.sh
export COMPOSE_PROJECT_NAME="${COMPOSE_PROJECT_NAME:-mass-l3-sil}"

case "$action" in
  up)
    echo "=== [gnc-profile] starting L3 sil-nodes stack (domain 42, host net) ==="
    docker compose up -d --build
    echo ""
    echo "=== [gnc-profile] starting GNC stack (domain 50, host net) ==="
    # GNC stack is a separate compose project (codex-gnc) with its own --profile gnc.
    docker compose -f docker-compose.gnc.yml --profile gnc up -d --build
    echo ""
    echo "=== [gnc-profile] stack up. Orchestrator: ${ORCH_URL} ==="
    echo "Run a probe: python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho --profile gnc"
    ;;
  --down|down)
    echo "=== [gnc-profile] stopping GNC stack (codex-gnc) ==="
    docker compose -f docker-compose.gnc.yml --profile gnc down
    echo "(L3 sil-nodes stack left running; stop it separately if needed.)"
    ;;
  *)
    echo "Usage: $0 [up|--down]" >&2
    exit 2
    ;;
esac
