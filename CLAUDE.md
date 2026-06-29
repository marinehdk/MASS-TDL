# CLAUDE.md

Canonical detailed project rules live in `AGENTS.md`. This file is the <=100-line Claude Code quick context; when detail conflicts or is missing, read `AGENTS.md` and follow it. Durable rule changes go to `AGENTS.md` first, then refresh this summary.

## Scope

- Repo = MASS L3 Tactical Decision Layer in chain: `L1 Mission -> L2 Voyage -> L3 Tactical -> L4 Guidance -> L5 Control`.
- L3 consumes L2 waypoints + fusion/NavFilter state, outputs `psi_cmd` / `u_cmd` / `ROT` to L4, and accepts X-axis checker vetoes.
- Reference-only docs: `docs/Init From Zulip/`, `docs/Init From SINAN/`; keep read-only.
- DDS interface fields must preserve `stamp`, `schema_version`, `confidence [0,1]`, and `rationale` when part of contract.

## Module map

- M1 ODD/Envelope: scheduler + only safety-context authority.
- M2 World Model: authoritative world view + COLREG geometry pre-classification.
- M3 Mission Manager: voyage plan, ETA, replanning triggers.
- M4 Behavior Arbiter: behavior dictionary + IvP-style arbitration.
- M5 Tactical Planner: Mid-MPC / BC-MPC route generation toward L4.
- M6 COLREGs Reasoner: ODD-aware rule reasoning.
- M7 Safety Supervisor: independent doer-checker path.
- M8 HMI/Transparency: only ROC/captain-facing transparency surface.

## Architecture invariants

- ODD alone controls safety context and behavior switching; no independent module-local safety truth.
- M7 checker/MRC path must stay simpler than doer and implementation-independent.
- Preserve CMM-style `current_state()`, `rationale()`, `forecast(delta_t)+uncertainty()` where applicable.
- Decision core stays vessel-agnostic; no product/vessel-specific architecture branches.
- Certification constraints are first-class: CCS i-Ship, IMO MASS Code ODD visibility, IEC 61508 SIL2 for M1/M7/MRC, ISO 21448 SOTIF, TMR >= 60 s.

## COLREGs debugging discipline

- Debug avoidance defects as full lifecycle: `L2 route/speed -> M2 world/CPA/geometry -> M6 rule/role/direction/release -> M4 FSM -> M5 trajectory/status -> L4 execution -> M7 veto/MRM -> M8 evidence`.
- Do not tune thresholds, scenario geometry, scorer gates, or one module output just to make one probe green.
- No mocks, skips, forced PASS paths, vessel-specific branches, or scenario-id conditionals.
- First classify failed stage contract from trace evidence; acceptable fix must explain upstream inputs, internal state, output message, and downstream behavior.
- Treat M5 `NORMAL`/`DEGRADED` oscillation as chain fault until solver health, fallback/recovery, behavior mode, route hash, waypoint validity, and L4/lifecycle takeover are separated.
- Recovery/route return must preserve past-and-clear, no crossing ahead, ample time, CPA/risk floor, and stable return-to-route together.

## Design docs

- Before D-task read: `docs/Design/00-master-plan.md` -> relevant `docs/Design/Phase N/00-overview.md` -> D spec -> relevant `TDL-Kernel/M{n}-*/M{n}-progress.md`.
- Architecture entry: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` sections 1-4 + M1-M8.
- Other key entries: `docs/Design/SIL/v1.0-unified/`, `docs/Design/Cross-Team Alignment/RFC-decisions.md`.
- Avoid archived/deprecated truth unless doing history: `*/archive/`, `docs/Design/Phase 0/Archive/`, `Archive/Old Modules/`, deprecated `Detailed Design/`.
- Design changes need traceable claims, interface in/out/frequency/confidence/rationale, degraded paths (`DEGRADED`, `CRITICAL`, `OUT-of-ODD`), CCS mapping, confidence labels.
- Edit only the touched module chapter/contract table; report other-module issues instead of silently editing.
- Use allocated bibliography `[Rx]`; no bare URLs in architecture docs.
- `docs/Doc From Claude/` append-only. Do not hand-edit `.nlm/` or `.claude/settings.local.json`.
- New D-task flow: spec -> plan -> evidence -> report -> update `M{n}-progress.md`; use dotted IDs like `D1.3.1`.

## Branches, worktrees, and promotion

- Local/GitHub main branch = `main`; A4000 GitLab main/sync target = `l3-tdl`.
- Do not commit directly on `main`; use short-lived task branches/worktrees.
- Primary checkout `/Users/marine/Code/MASS-L3-Tactical Layer` is integration surface, not shared dirty dev area.
- Put parallel worktrees under `.worktrees/`; never let two agents edit same worktree/branch unless explicitly handed off.
- Keep diffs surgical, run targeted tests in the touched worktree, and report changed paths + commands + results + A4000 need.
- Integration: merge tested feature branches into integration branch, rerun targeted tests + local gate, sync touched paths to A4000, run A4000 gate, then fast-forward `main` and push GitHub `main` + GitLab `l3-tdl`.

## Runtime gates

- Local gate must pass before any A4000 sync; A4000 gate must pass before GitHub/GitLab push.
- Local A4000-equivalent gate:
  ```bash
  source scripts/local-a4000-env.sh
  ./scripts/local-a4000-acceptance.sh
  ```
- Local compose env: `COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml`, `COMPOSE_PROFILES=plugins`, `ROS_DOMAIN_ID=42`.
- Main demo project = `COMPOSE_PROJECT_NAME=mass-l3-sil`; ports: orchestrator `18000`, Foxglove `18765`, Martin `3000`, Vite `5173`. Feature stacks must use separate compose project names.
- Do not stop/reclaim main demo stack unless task explicitly targets main stack or user prioritizes it.

## A4000

- Default login: `ssh a4000` as `marine.huang`; do not use `mass` account for TDL deploy/verify.
- A4000 ports: `18000`, `18765`, `5173`; do not touch production `8000` or `8765`.
- Verify TDL checkout path before first copy; do not create duplicate checkout if one exists.
- Sync only touched paths via `rsync -avR` or `scp`. No `rsync --delete`, repo-wide overwrite, `git pull`, `git reset`, or broad replacement without explicit clean-host approval.
- Normal A4000 flow: `source scripts/a4000-env.sh`, `npm run sys:start`, `./scripts/a4000-acceptance.sh`.
- Avoid `./scripts/a4000-acceptance.sh --sync` on dirty host unless explicitly approved.
- If `env_disturbance` wedges, suspect concurrent configure drivers; only one driver configures SIL at a time; reset with `docker compose restart sil-nodes`.

## Build/cache and tools

- Preserve BuildKit cache mounts in `sil_nodes.Dockerfile`: `# syntax=docker/dockerfile:1.5` and `--mount=type=cache`.
- `/root/.ccache` is shared; `/opt/ws/build` is private colcon state; avoid concurrent writers.
- CodeGraph index lives in `.codegraph/`. After creating/entering worktree, run `codegraph init` and verify `codegraph status .` before relying on it.
- When using CodeGraph MCP, set `projectPath` to the active worktree; never query feature work through primary checkout index.
- For codebase questions, use CodeGraph first (`codegraph_explore` or CLI fallback) before broad grep/read.

## Research, memory, handoff

- Project NLM notebooks: `safety_verification`, `maritime_regulations`, `colav_algorithms`, `maritime_human_factors`, `ship_maneuvering`, `silhil_platform`, `cybersecurity`; route via `.nlm/config.json`.
- MemPalace wing for project drawers: `mass_l3_tactical_layer`; use `mempalace wake-up --wing MASS-L3` or MCP diary read before meaningful work unless trivial/down.
- Persist key decisions/gotchas with MemPalace; before end/compact write AAAK summary; do not run retired `archive_to_headroom.py`.
- After meaningful work, append curated handoff to `handoff/workspace_log.md` with date, agent, commit, goal, changes, status, notes.
