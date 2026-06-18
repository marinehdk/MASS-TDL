## project scope and architecture

This repository is the L3 Tactical Decision Layer for the MASS stack. Treat it as the tactical layer in the five-layer chain:
`L1 Mission[hrs~days] -> L2 Voyage[min~hrs] -> L3 Tactical[sec~min] -> L4 Guidance[100ms~1s] -> L5 Control[10ms~100ms]`.

L3 consumes L2 waypoints and fusion/NavFilter state, outputs `psi_cmd` / `u_cmd` / `ROT` toward L4, and accepts X-axis deterministic checker vetoes. `docs/Init From Zulip/` and `docs/Init From SINAN/` are interface references only; keep them read-only.

Module map:
- M1 ODD/Envelope: TDL scheduler and only safety-context authority, 0.1-1 Hz.
- M2 World Model: authoritative world view plus COLREG geometry pre-classification, 10-50 Hz.
- M3 Mission Manager: voyage plan, ETA, and replanning triggers.
- M4 Behavior Arbiter: behavior dictionary and IvP-style multi-objective arbitration, 1-4 Hz.
- M5 Tactical Planner: Mid-MPC / BC-MPC route generation toward L4.
- M6 COLREGs Reasoner: ODD-aware rule reasoning.
- M7 Safety Supervisor: independent doer-checker path.
- M8 HMI/Transparency: only ROC/captain-facing transparency surface.

ROS2 DDS messages must preserve `stamp`, `schema_version`, `confidence` in `[0,1]`, and `rationale` when those fields are part of the interface contract.

## architecture invariants

- ODD is the only authority for safety context and behavior switching. Do not let modules keep independent "is safe" state that changes behavior.
- M7 doer-checker must stay simpler than the doer, with implementation independence. Avoid shared code, data structures, or complex dependencies on the MRC/checker path.
- Each module should expose or preserve CMM-style `current_state()`, `rationale()`, and `forecast(delta_t)+uncertainty()` semantics where applicable.
- Decision-core code must stay vessel-agnostic. Do not add `if vessel == ...` style FCB/product-specific constants to the architecture layer.
- Certification-facing constraints are first-class: CCS i-Ship auditability, IMO MASS Code ODD visibility, IEC 61508 SIL2 for M1/M7/MRC dependencies, ISO 21448 SOTIF for degraded perception, and TMR >= 60 s.

## design and documentation rules

- Before a D-task, read in order: `docs/Design/00-master-plan.md`, the relevant `docs/Design/Phase N/00-overview.md`, the relevant D spec, then the relevant `TDL-Kernel/M{n}-*/M{n}-progress.md`.
- Authoritative design entry points are `docs/Design/00-master-plan.md`, `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`, `docs/Design/SIL/v1.0-unified/`, and `docs/Design/Cross-Team Alignment/RFC-decisions.md`.
- For architecture questions, use `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` as the main architecture file. Use its table of contents plus sections 1-4 for the skeleton, and module sections M1-M8 for details.
- Do not treat archived docs as current truth. Avoid `*/archive/`, `docs/Design/Phase 0/Archive/`, `Archive/Old Modules/`, and deprecated `Detailed Design/` unless explicitly doing historical comparison.
- A design change is complete only when claims are traceable to source sections, `[Rx]` references, repo evidence, or NLM sources; interfaces list in/out messages, frequencies, confidence/rationale fields; degradation paths cover `DEGRADED`, `CRITICAL`, and `OUT-of-ODD`; CCS mapping and confidence labels are updated where relevant.
- When editing one module's docs, edit only that module's chapter and interface contract table. If another module has a problem, report it instead of silently editing it.
- References must use allocated `[Rx]` entries in the document bibliography. Do not paste bare URLs into architecture docs.
- Keep `docs/Doc From Claude/` append-only. Do not hand-edit `.nlm/` or `.claude/settings.local.json`; tool-managed files stay tool-managed.
- New D-task document flow is spec -> plan -> evidence -> report -> update `M{n}-progress.md`. Use numeric dotted IDs such as `D1.3.1`.

## branch / remote conventions

- 本地开发与 GitHub 以 `main` 作为主分支。
- A4000 上的 GitLab 仓库因权限限制，以 `l3-tdl` 作为主分支/同步目标。
- 涉及 A4000 部署、验收或 GitLab 同步时，目标分支使用 `l3-tdl`；不要把 A4000 GitLab 主线误判为 `main`。
- Do not commit directly on `main`; use task branches and merge only after the relevant local and A4000 gates pass.

## parallel Codex development workflow

Hard rule: each concurrent Codex development thread works in its own local branch/worktree. The primary checkout is the integration surface for tested branches, not a shared dirty development area.

Recommended layout:
- Keep the primary checkout at `/Users/marine/Code/MASS-L3-Tactical Layer` for integration, final local `main`, release notes, and promotion commands.
- Put parallel work under project-local `.worktrees/`, for example:
  ```bash
  git worktree add .worktrees/backend-colregs -b codex/backend-colregs main
  git worktree add .worktrees/frontend-map -b codex/frontend-map main
  ```
- Use one short-lived branch per task or subsystem. Example split: backend COLREGs/adapter logic, frontend SIL/HMI display, scenario/gate tooling.
- Do not let two Codex threads edit the same worktree or branch unless the user explicitly asks for a handoff.

Main runtime container rule:
- Treat the local `main` runtime stack as the stable demo/verification surface. When persistent `main` containers are needed, run them from a dedicated clean worktree such as `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/main-runtime`, not from a dirty feature checkout.
- The default `main` stack uses `COMPOSE_PROJECT_NAME=mass-l3-sil` and owns the normal local demo ports: orchestrator `18000`, Foxglove `18765`, Martin tiles `3000`, and Vite `5173`. Feature work must not take these ports from a running `main` demo stack.
- Feature or bugfix container work must use a new isolated worktree plus a task-specific compose project name, for example `COMPOSE_PROJECT_NAME=codex-colregs-fix`. Do not reuse `mass-l3-sil` for experiments unless the task explicitly targets the `main` runtime stack.
- If a temporary feature stack must use the normal demo ports, stop or move only the conflicting feature stack first. Do not stop the `main` stack unless the user explicitly prioritizes the feature stack or the task is to repair `main`.
- When repairing the `main` stack, first confirm its source mounts point at the intended `main` worktree, then rebuild/recreate only the `mass-l3-sil` project.

Per-thread completion contract before integration:
- Keep diffs surgical and limited to the task.
- Run targeted tests for the touched code in that worktree.
- Report changed paths, test commands, test results, and whether A4000 validation is required.
- Do not push feature branches to GitHub/GitLab before the integration gate below passes.
- Clean up after the worktree is finished: stop its compose project with the same `COMPOSE_PROJECT_NAME`, remove task-owned orphan containers, stop detached Vite/tmux sessions, and remove the worktree after its branch is merged or abandoned.
- Space cleanup must be scoped. Remove task-owned build/install/log artifacts, `web/node_modules`, generated runs, and images only when they belong to that worktree/project. Do not prune shared Docker volumes, shared images, or the `main` runtime stack without explicit approval.

Integration flow:
1. Start from current local `main` in the primary checkout.
2. Create a short-lived integration branch, for example `codex/integration-YYYYMMDD`.
3. Merge tested feature branches into the integration branch one by one.
4. Resolve conflicts only in the integration branch unless the fix clearly belongs back in a feature branch.
5. Run targeted tests again after merges, then run the local OrbStack gate from the next section.
6. After local gate passes, sync only touched paths from the integration result to A4000 and run A4000 verification.
7. After local and A4000 gates both pass, fast-forward local `main` from the integration branch.
8. Push local `main` to GitHub `main` and GitLab `l3-tdl`.

If any gate fails, fix locally first, rerun the local targeted tests and local OrbStack gate, then resync touched paths to A4000. Do not repair by editing directly on A4000, and do not use A4000 as the first real test host.

## local-first deployment gate

Hard rule: local OrbStack gate must pass before any A4000 sync; A4000 gate must pass before any GitHub/GitLab push.

Required order:
1. Develop in local branch/worktree.
2. Run targeted local tests for touched code.
3. Run local A4000-equivalent container gate:
   ```bash
   source scripts/local-a4000-env.sh
   ./scripts/local-a4000-acceptance.sh
   ```
4. Only after local gate passes, sync touched paths to A4000.
5. Run A4000 acceptance/probe.
6. Only after both gates pass, push to GitHub and GitLab.

Do not push code to GitHub/GitLab first and use A4000 as the first real test host. The local checkout is source of truth; A4000 is a runtime validation host.

## local OrbStack stack

- Local A4000-equivalent compose env: `COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml`.
- Plugin-local profile env: `COMPOSE_PROFILES=plugins`.
- Local isolated DDS domain: `ROS_DOMAIN_ID=42`.
- Local ports: orchestrator `https://127.0.0.1:18000`, Foxglove `18765`, Martin tiles `http://localhost:3000/`, Vite `http://localhost:5173/`.
- Local acceptance evidence is written under `runs/local_a4000_container_probe_*.json` and `runs/local_runtime_probe_*.json`.
- `scripts/local-a4000-acceptance.sh` must run from the checkout/worktree under test. If the local `mass-l3-sil` compose project was created from another checkout, the script fails fast by default. Stop that stack first, or rerun with `RECLAIM_STALE_LOCAL_PROJECT=1` to recreate it for the current worktree before collecting evidence.
- Runtime Console container control mounts `/var/run/docker.sock` only through the A4000/local compose override, not base compose.
- Inactive plugin candidates may be created but stopped for hot switching; the runtime gate still requires exactly one running plugin per role.
- Main demo stack startup from a clean `main` worktree:
  ```bash
  source scripts/local-a4000-env.sh
  COMPOSE_PROJECT_NAME=mass-l3-sil docker compose up -d --build
  ```
- Frontend dev server for screen 2 `仿真检查`:
  ```bash
  cd web
  ORCH_PORT=18000 FOX_PORT=18765 npm run dev -- --host 0.0.0.0
  ```
- If a persistent local frontend is needed, run it in a detached shell/tmux session; do not treat Vite as part of the Docker acceptance gate.
- Screen 02 `仿真检查` runtime surface is the Runtime Console for TDL core readiness and one-active-plugin-per-role external plugin checks. Do not add extra display-only dependencies or subscribe to nonessential external data.

## A4000 sync and verification

- Default TDL deployment and verification login: `ssh a4000`, configured for the personal account `marine.huang`. This is the only default target for syncing this repository, running the TDL stack, and collecting acceptance evidence.
- Do not use the A4000 `mass` account as the TDL deployment or verification environment. Treat `mass` as a shared upload/staging area for teammate code only.
- Do not write plaintext passwords into repo docs, scripts, commits, logs, or prompts. Use SSH key/agent or interactive login.
- Known shared external module paths on A4000:
  - Hydrodynamics and route planning: `/home/mass/simulation/`
  - Sensor fusion ROS2 workspace: `/home/mass/yougc/ros2_ws`
- A4000 ports are orchestrator `18000`, Foxglove `18765`, and Vite `5173`. Do not touch production ports `8000` or `8765`.
- TDL checkout path must be verified before first copy. Current runbook example:
  ```bash
  ssh a4000 'whoami; pwd; ls -la ~'
  # Set A4000_TDL to the verified TDL checkout under the marine.huang account.
  ```
  Do not create a second TDL checkout if one already exists.
- Sync only touched paths with `rsync -avR` or `scp`. Never use `rsync --delete`, repo-wide overwrite, `git pull`, `git reset`, or broad checkout replacement on A4000 unless the user explicitly approves it for a clean host.
- Normal A4000 flow after narrow sync:
  ```bash
  source scripts/a4000-env.sh
  npm run sys:start
  ./scripts/a4000-acceptance.sh
  ```
- Do not use `./scripts/a4000-acceptance.sh --sync` in normal dirty-host work. It may pull/reset host state; only use it after explicit user approval and clean-host confirmation.
- If scenario configuration wedges around `env_disturbance`, suspect concurrent configure drivers. Only one driver should configure SIL at a time; reset with `docker compose restart sil-nodes`.
- For external adaptor testing, switch profile and keep JSON evidence:
  ```bash
  curl -sk -X POST "${ORCH_URL}/api/v1/integration/profile" \
    -H 'Content-Type: application/json' \
    -d '{"name":"a4000_external"}'
  curl -sk -X POST "${ORCH_URL}/api/v1/integration/probe" \
    | tee runs/a4000_external_adapter_probe_$(date +%Y%m%d_%H%M%S).json
  ```

## promotion rule

- A branch is promotable only when local targeted tests, local OrbStack gate, A4000 acceptance, and required adaptor probe all pass.
- After promotion gate passes:
  - GitHub target remains `main`.
  - GitLab target remains `l3-tdl`.
  - Include local evidence path and A4000 evidence path in handoff/PR notes.
- If either local or A4000 gate fails, fix locally first, rerun local gate, then resync touched paths to A4000.

## Docker and build cache

- `sil_nodes.Dockerfile` uses BuildKit cache mounts. Do not remove `# syntax=docker/dockerfile:1.5` or `--mount=type=cache`; doing so turns incremental builds into full rebuilds.
- Treat `/root/.ccache` as shared ccache state. Treat `/opt/ws/build` as private colcon state; do not create concurrent writers to the same colcon build directory.
- New Dockerfiles that run `colcon build` should follow the same cache pattern.

## codegraph

This project uses CodeGraph as the code index. The index lives in `.codegraph/`; do not probe legacy graph-index paths.

Rules:
- For codebase questions, call `codegraph_explore` first. Use it for "how does X work", architecture, bug tracing, "where is X", and area surveys; one capped call usually returns the relevant source grouped by file.
- For focused follow-up, use `codegraph_search`, `codegraph_callers`, `codegraph_callees`, `codegraph_impact`, `codegraph_node`, `codegraph_files`, and `codegraph_status`.
- If the current Codex/Desktop thread does not expose the MCP tools, use the CodeGraph CLI fallback: `codegraph query`, `codegraph callers`, `codegraph callees`, `codegraph impact`, `codegraph files`, and `codegraph status`.
- Avoid broad grep or full-file reads before CodeGraph gives coordinates. Use raw source reads only to confirm a specific detail CodeGraph did not cover.
- The CodeGraph watcher normally syncs writes in about 1 second. No manual update is needed after edits; if `codegraph status .` shows pending files or the watcher is unavailable, run `codegraph sync .`.

## project research, memory, and handoff

- Project NLM domain notebooks: `safety_verification`, `maritime_regulations`, `colav_algorithms`, `maritime_human_factors`, `ship_maneuvering`, `silhil_platform`, and `cybersecurity`. Use `.nlm/config.json` routing before introducing new research paths.
- MemPalace is the project-local memory authority. Headroom is compression only, not the memory authority. Use the `mass_l3_tactical_layer` wing for all project drawers. The three-phase memory discipline is **mandatory** (no auto-save hooks; ZCode does not parse Claude hooks, so this relies on agent self-discipline per session):
  - **Session start**: run `mempalace wake-up --wing MASS-L3` (CLI) or call `mempalace_diary_read` (MCP) to load recent context (~600-900 tokens) before doing meaningful work. Skip only if the task is trivial or mempalace is down.
  - **Before session end / context compaction**: write one AAAK-format summary via `mempalace_diary_write` (task goal / key decisions / artifacts produced / open items). This is the substitute for the non-existent PreCompact hook.
  - **At each key decision point** (design choice, interface contract, non-obvious fix, gotcha): persist via `mempalace_add_drawer` so future sessions can retrieve it with `mempalace search "<keywords>"`.
  - HNSW corruption symptom (`status` reports `quarantined` / repeated `.drift-*` segments): run `mempalace repair --yes`. After a full re-embed of ~110k drawers on v3.4.1, `legacy` rebuild mode recovered all drawers while `from-sqlite` mode truncated to ~10%; prefer `legacy` unless the chromadb client itself cannot open the collection.
- After meaningful work, append a curated handoff entry to `handoff/workspace_log.md` using: `## [date] Agent / Git Commit / Task Goal / Core Changes / Current Status / Handoff Notes`.
- Do not run retired `archive_to_headroom.py`.
