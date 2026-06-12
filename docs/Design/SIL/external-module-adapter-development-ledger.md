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
| 1 | Backend profile loader | 019eb9ca-b819-7280-8fe4-0e0a057f1c74 | DONE | APPROVED 019eb9d5-9387-78a2-8023-1b7b0623595d | APPROVED 019eb9da-b03e-7561-ba98-ddc6f5345e4b | d650d3040d3e737e8fc76d9294fd7c917e35d9a4 |
| 2 | Backend probe service and integration routes | 019eb9ee-abb9-7c10-8a03-46a4d8ee2a37 | DONE | APPROVED 019eb9f3-5349-7702-8ed1-4ab1545f53c3 | APPROVED 019eb9f8-0c2b-7bb2-a6eb-dc5f6ba411f9 | c33ac112 |
| 3 | External adapter package and pure converters | 019eb9cb-1377-7791-a4db-cecfd8e7221c | DONE | APPROVED 019eb9e4-7233-7470-b484-c89b2a986dcf | APPROVED 019eb9e9-f698-7f11-93d3-9c1d50fec38b | ea951232482ca67000032448a4a49bdf0be4aaf1 |
| 4 | Neutral IPC and TDL ingress node | 019eb9ef-7a14-7512-bf4e-abeb261091a1 | DONE | APPROVED 019eb9f4-aa57-7e51-a766-08b5dedf01ae | APPROVED 019eb9fd-b71f-73f3-a3d4-de5ee1be247c | 088d2a774a5e2caad756e6fd6aae1dd977d25f5b |
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
- Task 1 final code review approved by `019eb9da-b03e-7561-ba98-ddc6f5345e4b`; final commit `d650d3040d3e737e8fc76d9294fd7c917e35d9a4`.
- Task 3 fixes now include entrypoint stubs and string target classification; final review pending on commit `575fc1428c309dd63fb927cd5f248be0ba24a228`.
- Task 3 final code review approved by `019eb9e9-f698-7f11-93d3-9c1d50fec38b`; final commit `ea951232482ca67000032448a4a49bdf0be4aaf1`.
- Task 2 dispatched to `019eb9ee-abb9-7c10-8a03-46a4d8ee2a37`.
- Task 4 dispatched to `019eb9ef-7a14-7512-bf4e-abeb261091a1`.
- Task 2 implementation completed in commit `1e012b4b`; tests `tests/sil_orchestrator/test_integration_routes.py tests/sil_orchestrator/test_selfcheck.py` passed `9`.
- Task 4 implementation completed in commit `96e7cfcc73b1ca4643abffa35ab87b7504e04ac4`; IPC/ingress/converter tests passed `17`.
- Task 4 final code review approved by `019eb9fd-b71f-73f3-a3d4-de5ee1be247c`; final commit `088d2a774a5e2caad756e6fd6aae1dd977d25f5b`.
- Task 2 code review requested fixes: missing `ros2`, subprocess timeout, and nonzero topic info must return failed gate details rather than escaping to HTTP 500.
- Task 2 final code review approved by `019eb9f8-0c2b-7bb2-a6eb-dc5f6ba411f9`; final commit `c33ac112`.

## Blocking Issues

- `web/node_modules` is absent in the new worktree. Frontend task must run `npm install` in `web/` before Vitest/build.
