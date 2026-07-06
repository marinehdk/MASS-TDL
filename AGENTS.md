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

## COLREGs full-chain debugging rule

COLREGs avoidance defects must be debugged as an end-to-end encounter lifecycle, not as one scenario/one patch tuning. Before changing behavior logic, collect or add evidence for every active stage in the chain:
`L2 route/speed -> M2 world/CPA/geometry -> M6 rule/role/direction/release -> M4 behavior FSM -> M5 trajectory/status -> L4 guidance/execution -> M7 veto/MRM -> M8 evidence`.

Mandatory discipline:
- Do not tune thresholds, scenario geometry, scorer gates, or one module output just to turn a single probe green.
- Do not add mocks, skips, forced PASS paths, vessel-specific branches, or scenario-id conditionals.
- For each failed scenario, first classify which stage contract broke, using trace evidence. A fix is acceptable only when it explains why upstream inputs, internal state, output message, and downstream consumer behavior are all coherent.
- M5 `NORMAL`/`DEGRADED` oscillation is a first-class chain fault. Treat it as unresolved until the trace separates solver health, fallback/recovery mode, behavior mode, route hash stability, valid-waypoint state, and L4/lifecycle takeover state.
- L2 route or speed-profile republishing, M2 target identity churn, M6 encounter FSM re-arm, M4 behavior latch churn, M5 solver/fallback flips, L4 route-following override, and M7 veto must be checked together before any code change.
- Recovery and route return must preserve COLREGs semantics: past-and-clear, no crossing ahead, ample time, CPA/risk floor, and stable return-to-route all have to hold together.

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

- 本仓库的 remote 是 GitLab (`origin` = `git@gitlab.sangoai.com:mass_devgroup/01-dynamics/01-simulation.git`)。本机 A4000 上没有独立的 GitHub remote；不要假设存在 `github` remote 或把分支 push 到 GitHub。
- 开发与集成的 source of truth 是 `l3-tdl` 分支（本地 + `origin/l3-tdl`）。远程仓库同时存在 `main` 与 `master`，但当前工作主线是 `l3-tdl`，不要把 GitLab 主线误判为 `main`/`master`。
- 任务分支命名沿用 `codex/<task>` / `feat/<task>` / `fix/<task>`；集成完成后合并回 `l3-tdl` 并 `git push origin l3-tdl`。
- Do not commit directly on `l3-tdl`; use task branches/worktrees and merge only after the acceptance gate in the next section passes.
- 同步规则（本机即 A4000，无 ssh/rsync 跨机）：只 push 到 `origin/l3-tdl`。涉及外部模块联调时，再单独处理 `/home/mass/...` 路径（见下）。

## parallel Codex development workflow

Hard rule: each concurrent Codex development thread works in its own local branch/worktree. The primary checkout is the integration surface for tested branches, not a shared dirty development area.

本机就是 A4000（`tigerwang-System-Product-Name`，Ubuntu 22.04 x86_64，账户 `marine.huang`）。开发、容器构建、仿真验收都在这一台机器上完成，没有跨机 ssh/rsync。

Recommended layout:
- Keep the primary checkout at `/home/marine.huang/Code/mass-l3`（分支 `l3-tdl`）作为集成面：集成、release notes、promotion 命令都在这里执行。
- Put parallel work under project-local `.worktrees/`, for example:
  ```bash
  git worktree add .worktrees/backend-colregs -b codex/backend-colregs l3-tdl
  git worktree add .worktrees/frontend-map -b codex/frontend-map l3-tdl
  ```
- Use one short-lived branch per task or subsystem. Example split: backend COLREGs/adapter logic, frontend SIL/HMI display, scenario/gate tooling.
- Do not let two Codex threads edit the same worktree or branch unless the user explicitly asks for a handoff.

Main runtime container rule:
- Treat the `l3-tdl` runtime stack as the stable demo/verification surface. When persistent demo containers are needed, run them from a dedicated clean worktree such as `/home/marine.huang/Code/mass-l3/.worktrees/main-runtime`，而不是从脏的 feature checkout。
- The default demo stack uses `COMPOSE_PROJECT_NAME=mass-l3-sil` and owns the normal demo ports: orchestrator `18000`, Foxglove `18765`, Martin tiles `3000`, and Vite `5173`. Feature work must not take these ports from a running demo stack.
- Feature or bugfix container work must use a new isolated worktree plus a task-specific compose project name, for example `COMPOSE_PROJECT_NAME=codex-colregs-fix`. Do not reuse `mass-l3-sil` for experiments unless the task explicitly targets the demo stack.
- If a temporary feature stack must use the normal demo ports, stop or move only the conflicting feature stack first. Do not stop the demo stack unless the user explicitly prioritizes the feature stack or the task is to repair the demo stack.
- When repairing the demo stack, first confirm its source mounts point at the intended `l3-tdl` worktree, then rebuild/recreate only the `mass-l3-sil` project.

Per-thread completion contract before integration:
- Keep diffs surgical and limited to the task.
- Run targeted tests for the touched code in that worktree.
- Report changed paths, test commands, test results.
- Do not push feature branches to GitLab before the acceptance gate below passes.
- Clean up after the worktree is finished: stop its compose project with the same `COMPOSE_PROJECT_NAME`, remove task-owned orphan containers, stop detached Vite/tmux sessions, and remove the worktree after its branch is merged or abandoned.
- Space cleanup must be scoped. Remove task-owned build/install/log artifacts, `web/node_modules`, generated runs, and images only when they belong to that worktree/project. Do not prune shared Docker volumes, shared images, or the demo runtime stack without explicit approval.

Integration flow（单机，无跨机同步）:
1. Start from current `l3-tdl` in the primary checkout.
2. Create a short-lived integration branch, for example `codex/integration-YYYYMMDD`.
3. Merge tested feature branches into the integration branch one by one.
4. Resolve conflicts only in the integration branch unless the fix clearly belongs back in a feature branch.
5. Run targeted tests again after merges, then run the acceptance gate from the next section.
6. After the gate passes, fast-forward `l3-tdl` from the integration branch.
7. Push `l3-tdl` to `origin/l3-tdl`（GitLab）。

If any gate fails, fix in the worktree first, rerun the targeted tests and the acceptance gate, then redo the integration merge. Do not repair by editing directly on the demo stack checkout.

## local-first deployment gate

Hard rule: 本机即 A4000，验收 gate 必须在本机通过后才能 push 到 GitLab `origin/l3-tdl`。没有跨机 "local OrbStack → A4000" 二段式 gate；本机的容器验收就是 A4000 验收。

Required order:
1. Develop in a task branch/worktree（`.worktrees/<task>`）。
2. Run targeted local tests for touched code（colcon test / gtest 二进制）。
3. Run the A4000 本机 acceptance gate（即标准 A4000 验收）:
   ```bash
   source scripts/a4000-env.sh        # 本机用 a4000-env.sh，不再用 local-a4000-env.sh
   npm run sys:start
   ./scripts/a4000-acceptance.sh
   ```
4. Only after the gate passes, merge into `l3-tdl` and `git push origin l3-tdl`。

Do not push to GitLab first and use the running stack as the first real test host. The worktree checkout under test is source of truth; the running demo stack (`mass-l3-sil`) is the verification surface, not a development area.

## A4000 native container stack

本机用原生 Docker（非 OrbStack；`which orb` 无输出）。compose 环境由 `scripts/a4000-env.sh` 定义。

- A4000-native compose env: `COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml`。
- Plugin profile env: `COMPOSE_PROFILES=plugins`（仅外部插件联调时开启；默认 `TDL_RUNTIME_PROFILE=internal-local` 无外部插件）。
- Isolated DDS domain: `ROS_DOMAIN_ID=42`。
- Ports: orchestrator `https://127.0.0.1:18000`, Foxglove `18765`, Martin tiles `http://localhost:3000/`, Vite `http://localhost:5173/`。不要碰生产/共享端口 `8000`、`8765`（jitsi 等服务占用）。
- Acceptance evidence 写在 `runs/local_a4000_container_probe_*.json` 和 `runs/local_runtime_probe_*.json`。
- `scripts/local-a4000-acceptance.sh` 仍可用于"非 `mass-l3-sil` 项目"的本机等价验收，但默认 gate 是 `scripts/a4000-acceptance.sh`（本机即 A4000）。若 `mass-l3-sil` compose 项目是从别的 checkout 创建的，脚本会 fail-fast；先停掉该 stack，或用 `RECLAIM_STALE_LOCAL_PROJECT=1` 在当前 worktree 重建后再收证据。
- Runtime Console 容器对 `/var/run/docker.sock` 的挂载只在 `docker-compose.a4000.yml` override 中存在，base compose 没有。
- Inactive plugin candidates may be created but stopped for hot switching; the runtime gate still requires exactly one running plugin per role.
- Demo stack startup from a clean `l3-tdl` worktree:
  ```bash
  source scripts/a4000-env.sh
  COMPOSE_PROJECT_NAME=mass-l3-sil docker compose up -d --build
  ```
- Feature/bugfix 容器工作必须用独立 worktree + 任务级 compose project name，例如 `COMPOSE_PROJECT_NAME=nlp-cpa-fix`（当前活跃示例：`nlp-cpa-fix-sil-nodes-1` 等）。不要把实验容器复用 `mass-l3-sil`。
- Frontend dev server for screen 2 `仿真检查`:
  ```bash
  cd web
  ORCH_PORT=18000 FOX_PORT=18765 npm run dev -- --host 0.0.0.0
  ```
- If a persistent local frontend is needed, run it in a detached shell/tmux session; do not treat Vite as part of the Docker acceptance gate.
- Screen 02 `仿真检查` runtime surface is the Runtime Console for TDL core readiness and one-active-plugin-per-role external plugin checks. Do not add extra display-only dependencies or subscribe to nonessential external data.

## A4000 verification（本机）

本机就是 A4000，不存在 `ssh a4000` 跨机部署/同步。开发、构建、容器验收、证据采集都在 `/home/marine.huang/Code/mass-l3` 这一台机器上完成。

- TDL 部署与验证账户：`marine.huang`（本机当前账户）。Checkout 路径：`/home/marine.huang/Code/mass-l3`（分支 `l3-tdl`）。不要创建第二个 TDL checkout。
- 不要把 A4000 的 `mass` 账户当作 TDL 部署/验证环境。`mass` 账户是队友代码的共享上传/暂存区，本仓库不部署到那里。
- Do not write plaintext passwords into repo docs, scripts, commits, logs, or prompts.
- 本机上的共享外部模块路径（外部联调用，访问权限以本机实际为准）:
  - Hydrodynamics and route planning: `/home/mass/simulation/`
  - Sensor fusion ROS2 workspace: `/home/mass/yougc/ros2_ws`
- Ports: orchestrator `18000`, Foxglove `18765`, Vite `5173`。不要碰生产/共享端口 `8000`、`8765`。
- Normal 本机 verification flow（无 sync 步骤）:
  ```bash
  source scripts/a4000-env.sh
  npm run sys:start
  ./scripts/a4000-acceptance.sh
  ```
- 不要用 `./scripts/a4000-acceptance.sh --sync`（脚本里的 sync 分支面向历史跨机场景，可能 reset 本机状态；仅在你显式确认 clean-host 后使用）。
- 版本管理一律走 git：在 worktree 里 commit/merge，最后 `git push origin l3-tdl`。不要用 `rsync --delete`、repo-wide overwrite、`git pull`/`git reset` 或 broad checkout replacement 去覆盖本机 checkout，除非用户显式批准 clean-host 重建。
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

- A branch is promotable only when targeted unit tests, the A4000 本机 acceptance gate, and any required adaptor probe all pass.
- After the gate passes:
  - Merge the integration branch into `l3-tdl`.
  - GitLab target remains `l3-tdl` (`origin/l3-tdl`)。本机无 GitHub remote，不要尝试 push `main` 到 GitHub。
  - Include the acceptance evidence path（`runs/...json`）in handoff/MR notes.
- If the gate fails, fix in the worktree first, rerun targeted tests and the acceptance gate, then redo the integration merge.

## Docker and build cache

- `sil_nodes.Dockerfile` uses BuildKit cache mounts. Do not remove `# syntax=docker/dockerfile:1.5` or `--mount=type=cache`; doing so turns incremental builds into full rebuilds.
- Treat `/root/.ccache` as shared ccache state. Treat `/opt/ws/build` as private colcon state; do not create concurrent writers to the same colcon build directory.
- New Dockerfiles that run `colcon build` should follow the same cache pattern.

## codegraph

This project uses CodeGraph as the code index. The index lives in `.codegraph/`; do not probe legacy graph-index paths.

Rules:
- After creating or entering a task worktree, run `codegraph init` from that worktree before relying on CodeGraph. Verify with `codegraph status .`; if it reports an index from another git working tree, treat results as stale routing only and initialize/sync the current worktree first.
- When calling CodeGraph MCP from Codex/Desktop, always set `projectPath` to the active worktree path. For CLI fallback, run `codegraph ... .` from that same worktree. Do not query a feature branch through the primary checkout index.
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
