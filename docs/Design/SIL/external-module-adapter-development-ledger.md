# External Module Adapter Development Ledger

Date: 2026-06-12

Branch: `codex/external-module-adapters`

Worktree: `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/external-module-adapters`

Spec: `docs/Design/SIL/external-module-adapter-spec.md`

Plan: `docs/superpowers/plans/2026-06-12-external-module-adapters.md`

## Execution Rules

1. Default TDL behavior must remain unchanged.
2. Do not add production external integration logic to `docker/sil_topic_bridge.py`.
3. Do not expose `/cmd_tau`, `/thruster/commands`, or actuator-level output through external adapters.
4. Keep external modules isolated by `ROS_DOMAIN_ID`; translate through profile-driven adapters.
5. Screen 02 `仿真检查` owns external integration readiness and GO blocking.
6. Local OrbStack must run the A4000-equivalent container gate before A4000 sync.
7. A4000 work uses patch/scp only; no `git pull`, `git reset`, or broad sync.

## Baseline

Command:

```bash
PYTHONDONTWRITEBYTECODE=1 pytest tests/sil_orchestrator/test_selfcheck.py tests/docker/test_route_ingest_node.py -q
```

Result: `8 passed, 2 warnings`

## Task Ledger

| Task | Scope | Agent | Implementation | Spec Review | Code Review | Commit |
|---|---|---|---|---|---|---|
| 1 | Backend profile loader | 019eb9ca-b819-7280-8fe4-0e0a057f1c74 | fixes requested | APPROVED 019eb9ce-2205-7c33-a2aa-1abec5e719ff | CHANGES_REQUESTED 019eb9ce-4dcb-7ce2-b6c4-f651861c0266 | 2536d09eed6a002f0f16d1de3bf2a7648abbe46f |
| 2 | Backend probe service and integration routes | pending | pending | pending | pending | pending |
| 3 | External adapter package and pure converters | 019eb9cb-1377-7791-a4db-cecfd8e7221c | fixes requested | APPROVED 019eb9cf-fad2-7b91-b09d-7d57da4b23de | CHANGES_REQUESTED 019eb9d0-2eaf-74b3-8fb0-967c7a606053 | 812f7a7dfc64c7c3ddca8b35783dde08dad2ee29 |
| 4 | Neutral IPC and TDL ingress node | pending | pending | pending | pending | pending |
| 5 | Route-out adapter | pending | pending | pending | pending | pending |
| 6 | Screen 02 external integration panel | pending | pending | pending | pending | pending |
| 7 | Profile-based launch wiring | pending | pending | pending | pending | pending |
| 8 | Local and A4000 integration verification | pending | pending | pending | pending | pending |

## Review Notes

- Task 1 spec review approved by `019eb9ce-2205-7c33-a2aa-1abec5e719ff`.
- User updated deployment strategy: local OrbStack must run A4000-equivalent container validation before A4000 sync.
- Task 1 code quality review requested fixes: deep immutable mappings, bool-int rejection, duplicate profile name rejection, schema alignment to `workspace_setup` and role adapters.
- Task 3 spec review approved by `019eb9cf-fad2-7b91-b09d-7d57da4b23de`.
- Task 3 code quality review requested fixes: stable route_id independent of stamp, converter dict shape aligned to TDL ingress/ROS message contract, speed profile per segment with coherent duration.

## Blocking Issues

- `web/node_modules` is absent in the new worktree. Frontend task must run `npm install` in `web/` before Vitest/build.
