# M5 Tactical Planner — Phase E1

Module implementing dual-layer MPC collision avoidance for the MASS L3 Tactical Layer.

## Architecture

- **Mid-MPC** (`mid_mpc/`): CasADi/IPOPT NLP, 1 Hz, 60–90 s horizon. Publishes `AvoidancePlan` → L4.
- **BC-MPC** (`bc_mpc/`): Branching-tree (Eriksen et al. 2020 [R20]), event-driven, 20 s horizon. Publishes `ReactiveOverrideCmd` → L4.
- **Shared** (`shared/`): `CpaCalculator`, `VesselDynamicsModel`, `ConstraintCompiler`.
- **Common** (`common/`): `units`, `types`, `sha256` (NIST FIPS 180-4).

## ASDR Signing

All `ASDRRecord` messages published by M5 carry a SHA-256 signature of `decision_json` in the `signature` field (per `l3_msgs/ASDRRecord.msg`). Signed in `MidMpcNode::on_solve_()` and `BcMpcNode::publish_override_()`. Algorithm: NIST FIPS 180-4, no external library, stack-only (no allocation).

## Topics

| Topic | Type | Dir | Rate |
|---|---|---|---|
| `/m2/world_state` | `WorldState` | Sub | 4 Hz |
| `/m2/own_ship_state` | `OwnShipState` | Sub | 10 Hz |
| `/m2/tracked_targets` | `TrackedTarget[]` | Sub | 4 Hz |
| `/m4/behavior_plan` | `BehaviorPlan` | Sub | 1 Hz |
| `/m6/colregs_constraint` | `COLREGsConstraint` | Sub | 1 Hz |
| `/l2/planned_route` | `MissionGoal` | Sub | on change |
| `/m5/avoidance_plan` | `AvoidancePlan` | Pub | 1 Hz |
| `/m5/reactive_override_cmd` | `ReactiveOverrideCmd` | Pub | evt / ≤ 10 Hz |
| `/m5/asdr_record` | `ASDRRecord` | Pub | 1 Hz |
| `/m5/asdr_record_bc` | `ASDRRecord` | Pub | event |
| `/m5/sat_data` | `SATData` | Pub | 1 Hz |

## `[TBD-HAZID]` Parameters

All safety-critical tunables in `BcMpcBranchFormulation::Config` and `BcMpcNode::Config` are annotated `[TBD-HAZID]` pending HAZID RUN-001 calibration (target: 2026-08-19). See `docs/Design/HAZID/RUN-001-kickoff.md` for the parameter inventory.

## Design Documents

- Detailed design: [`docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md`](../../docs/Design/Detailed%20Design/M5-Tactical-Planner/01-detailed-design.md)
- Code skeleton spec: [`docs/Implementation/M5/code-skeleton-spec.md`](../../docs/Implementation/M5/code-skeleton-spec.md)
- Architecture (v1.1.2): [`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`](../../docs/Design/Architecture%20Design/MASS_ADAS_L3_TDL_%E6%9E%B6%E6%9E%84%E8%AE%BE%E8%AE%A1%E6%8A%A5%E5%91%8A.md)

## References

- **[R20]** Eriksen, B., Bitar, G., Breivik, M. et al. (2020). *Branching-Course MPC for Collision Avoidance in Unstructured Environments.* Frontiers Robot AI, 7:11.
- **NIST FIPS 180-4** — SHA-256 specification (used for ASDR signing).
