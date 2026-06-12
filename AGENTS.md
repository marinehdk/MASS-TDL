## branch / remote conventions

- 本地开发与 GitHub 以 `main` 作为主分支。
- A4000 上的 GitLab 仓库因权限限制，以 `l3-tdl` 作为主分支/同步目标。
- 涉及 A4000 部署、验收或 GitLab 同步时，目标分支使用 `l3-tdl`；不要把 A4000 GitLab 主线误判为 `main`。

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

- Local A4000-equivalent compose env: `COMPOSE_FILE=docker-compose.yml:docker-compose.a4000.yml`.
- Local isolated DDS domain: `ROS_DOMAIN_ID=42`.
- Local ports: orchestrator `https://127.0.0.1:18000`, Foxglove `18765`, Vite `http://localhost:5173/`.
- Frontend dev server for screen 2 `仿真检查`:
  ```bash
  cd web
  ORCH_PORT=18000 FOX_PORT=18765 npm run dev -- --host 0.0.0.0
  ```
- If a persistent local frontend is needed, run it in a detached shell/tmux session; do not treat Vite as part of the Docker acceptance gate.
- The frontend integration surface belongs in screen 2 `仿真检查`; do not add extra display-only dependencies or subscribe to nonessential external data.

## A4000 sync and verification

- Default external-module integration account: `mass@A4000`.
- Do not write plaintext passwords into repo docs, scripts, commits, logs, or prompts. Use SSH key/agent or interactive login.
- Known external module paths on A4000:
  - Hydrodynamics and route planning: `/home/mass/simulation/`
  - Sensor fusion ROS2 workspace: `/home/mass/yougc/ros2_ws`
- TDL checkout path must be verified before first copy. Current runbook example:
  ```bash
  A4000_TDL=/home/mass/MASS-L3-Tactical-Layer
  ssh mass@A4000 'pwd; ls -la /home/mass'
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

## codegraph

This project uses CodeGraph as the code index. The index lives in `.codegraph/`; do not probe legacy graph-index paths.

Rules:
- For codebase questions, call `codegraph_explore` first. Use it for "how does X work", architecture, bug tracing, "where is X", and area surveys; one capped call usually returns the relevant source grouped by file.
- For focused follow-up, use `codegraph_search`, `codegraph_callers`, `codegraph_callees`, `codegraph_impact`, `codegraph_node`, `codegraph_files`, and `codegraph_status`.
- If the current Codex/Desktop thread does not expose the MCP tools, use the CodeGraph CLI fallback: `codegraph query`, `codegraph callers`, `codegraph callees`, `codegraph impact`, `codegraph files`, and `codegraph status`.
- Avoid broad grep or full-file reads before CodeGraph gives coordinates. Use raw source reads only to confirm a specific detail CodeGraph did not cover.
- The CodeGraph watcher normally syncs writes in about 1 second. No manual update is needed after edits; if `codegraph status .` shows pending files or the watcher is unavailable, run `codegraph sync .`.
