# A4000 Runtime and Integration Runbook

This file is the operational reference for MASS-L3 development, runtime isolation, verification, and promotion on the local A4000 host. Exact environment values remain owned by `scripts/a4000-env.sh` and the active Compose files; do not copy them into other policy documents.

## Repository and branch authority

- Repository: `/home/marine.huang/Code/mass-l3`.
- Integration source of truth: local `l3-tdl` and `origin/l3-tdl` on GitLab.
- Never commit production work directly on `l3-tdl`; use a short-lived task branch and project-local `.worktrees/<task>` checkout.
- Use `codex/<task>`, `feat/<task>`, or `fix/<task>` branch names.
- Integrate tested task branches through a short-lived integration branch, rerun verification, then fast-forward `l3-tdl` and push only `origin/l3-tdl`.
- Do not use repository-wide overwrite, `rsync --delete`, destructive reset, or acceptance-script sync as a substitute for Git integration.
- Never store plaintext passwords or secrets in repository files, commits, logs, or prompts.

## Worktree and runtime isolation

- One concurrent development task owns one branch, one worktree, and one Compose project.
- Primary checkout is the integration surface, not a shared dirty development directory.
- Persistent demo verification uses a clean `l3-tdl`-based worktree and Compose project `mass-l3-sil`.
- Feature work uses a task-specific `COMPOSE_PROJECT_NAME`; never reuse `mass-l3-sil` for experiments.
- Do not stop, rebuild, or take ports from the demo stack unless the task explicitly targets that stack.
- Before repairing the demo stack, confirm its source mounts point to the intended clean worktree. Recreate only services owned by `mass-l3-sil`.

## Environment authority

Load the active environment instead of reproducing its values:

```bash
source scripts/a4000-env.sh
```

The script and Compose overrides own `COMPOSE_FILE`, DDS domain, orchestrator URL, and Foxglove settings. Current reserved demo ports are orchestrator `18000`, Foxglove `18765`, Martin `3000`, and Vite `5173`; do not touch shared ports `8000` or `8765`.

- Enable plugin profiles only for external-plugin integration; internal-local remains the normal runtime.
- Runtime Console Docker-socket access belongs only in the A4000 override.
- Exactly one plugin per role may be running; inactive candidates may exist stopped for hot switching.
- Screen 02 `仿真检查` remains a core-readiness/plugin-role surface; do not add display-only runtime dependencies.

External-module integration paths:

- Hydrodynamics and route planning: `/home/mass/simulation/`
- Sensor fusion ROS2 workspace: `/home/mass/yougc/ros2_ws`

The `mass` account and these shared paths are external integration surfaces, not alternate TDL deployment checkouts.

## Canonical local acceptance gate

Run from the task worktree under test:

```bash
source scripts/a4000-env.sh
npm run sys:start
./scripts/a4000-acceptance.sh
```

Required order:

1. Targeted tests for touched code.
2. Local A4000 acceptance gate.
3. Required external-adapter probe, when applicable.
4. Integration into `l3-tdl` and push to `origin/l3-tdl`.

Do not use `./scripts/a4000-acceptance.sh --sync` without explicit clean-host authorization. If the gate reports that `mass-l3-sil` belongs to another checkout, stop the conflicting feature stack or deliberately reclaim/rebuild the local project from the worktree under test.

`scripts/local-a4000-acceptance.sh` is compatibility tooling for non-`mass-l3-sil` projects; it is not the default promotion gate.

## External adapter evidence

When the task changes an external integration boundary:

```bash
curl -sk -X POST "${ORCH_URL}/api/v1/integration/profile" \
  -H 'Content-Type: application/json' \
  -d '{"name":"a4000_external"}'
curl -sk -X POST "${ORCH_URL}/api/v1/integration/probe" \
  | tee runs/a4000_external_adapter_probe_$(date +%Y%m%d_%H%M%S).json
```

Record acceptance and adapter evidence paths in handoff or merge notes.

## Cleanup and build ownership

- Stop task-owned Compose projects with the same `COMPOSE_PROJECT_NAME` used to start them.
- Remove only task-owned containers, sessions, build/install/log trees, generated runs, and dependencies.
- Never prune shared Docker volumes, shared images, demo containers, or unrelated worktrees without explicit approval.
- Preserve BuildKit cache mounts in `sil_nodes.Dockerfile`.
- `/root/.ccache` is shared cache state; `/opt/ws/build` is private colcon state and must not have concurrent writers.
- If `env_disturbance` configuration wedges, first eliminate concurrent SIL configuration drivers; then restart only the task-owned `sil-nodes` service.
