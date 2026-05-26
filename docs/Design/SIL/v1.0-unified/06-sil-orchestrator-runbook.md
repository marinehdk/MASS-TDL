# SIL Orchestrator Runbook

## Known issue: Orchestrator lifecycle non-persistence

When the SIL orchestrator container restarts, lifecycle state (current scenario, active flag)
is not persisted. The next lifecycle/status query returns default state (scenario_id="",
ownship position=(30.5N,122E) Yangtze fallback).

**Workaround:** Always explicitly run the three-command activation sequence:
  1. POST /lifecycle/cleanup
  2. POST /lifecycle/configure {"scenario_id":"imazu-01-ho"}
  3. POST /lifecycle/activate

**Fix candidate (deferred):** Persist lifecycle state to local file or Redis; restore on container restart.

## R3 avoidance chain fix notes (2026-05-27)

After R3 code changes, the following restart sequence is required:

- **C++ changes** (M4, M5): `docker compose build sil-nodes` then `docker compose restart sil-nodes`
- **Python bridge changes**: `docker compose restart sil-orchestrator` (bridge runs inside orchestrator container; file is volume-mounted so no rebuild needed, but container restart is required for the Python process to pick up changes)
- **Full stack**: `npm run sys:stop && npm run sys:start`
