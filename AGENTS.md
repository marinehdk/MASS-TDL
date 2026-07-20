# MASS-L3 project agent contract

Global workflow, research, confidence, communication, and generic tool rules come from `~/.codex/AGENTS.md`. This file contains only MASS-L3 project deltas.

## Project scope and invariants

MASS-L3 is the Tactical Decision Layer in `L1 Mission -> L2 Voyage -> L3 Tactical -> L4 Guidance -> L5 Control`. Modules: M1 ODD authority; M2 world model/COLREG geometry; M3 mission tracking; M4 behavior arbitration; M5 Mid-MPC/BC-MPC; M6 COLREG reasoning; M7 independent checker/MRM; M8 operator transparency.

- L3 consumes L2 route/speed plus fused state; outputs route, heading, speed, and ROT intent toward L4; accepts deterministic checker vetoes.
- ODD is the only safety-context and behavior-switching authority.
- M7 checker/MRC remains simpler and implementation-independent from the doer.
- Decision-core code stays vessel-agnostic; no vessel-name or scenario-ID branches.
- Preserve CMM-style `current_state()`, `rationale()`, and `forecast(delta_t)+uncertainty()` semantics where applicable.
- Preserve contracted ROS2 `stamp`, `schema_version`, `confidence` in `[0,1]`, and `rationale` fields.
- Treat CCS auditability, IMO MASS ODD visibility, IEC 61508 SIL2 dependencies, ISO 21448 degradation, and TMR >= 60 s as first-class constraints.
- Keep `docs/Init From Zulip/` and `docs/Init From SINAN/` read-only.

## COLREGs full-chain debugging

Diagnose avoidance defects across the encounter lifecycle before changing behavior:
`L2 route/speed -> M2 world/CPA/geometry -> M6 rule/role/direction/release -> M4 behavior FSM -> M5 trajectory/status -> L4 execution -> M7 veto/MRM -> M8 evidence`.

- Classify the first broken stage contract from trace evidence; do not tune one scenario green.
- Never add mocks, skips, forced PASS paths, vessel-specific branches, or scenario-ID conditionals.
- Explain upstream input, internal state, output message, and downstream response coherently before accepting a fix.
- Treat M5 `NORMAL`/`DEGRADED` oscillation as unresolved until solver health, fallback/recovery, behavior mode, route hash, waypoint validity, and L4 takeover are separated.
- Check L2 republish, M2 identity, M6 re-arm/release, M4 latch, M5 fallback, L4 override, and M7 veto together.
- Recovery requires past-and-clear, no crossing ahead, ample time, CPA/risk floor, and stable route return together.

## Design authority

- D-task reading order: `docs/Design/00-master-plan.md` -> Phase overview -> D spec -> relevant `M{n}-progress.md`.
- Architecture authority: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`; also use `docs/Design/SIL/v1.0-unified/` and `docs/Design/Cross-Team Alignment/RFC-decisions.md`.
- Avoid archives and deprecated detailed-design trees unless performing explicit historical comparison.
- A design change needs traceable claims, complete interfaces/frequencies/CMM fields, degradation paths, and applicable certification mapping.
- Edit only the owning module chapter and interface table; report cross-module issues instead of silently editing them.
- Use allocated `[Rx]` references; no bare URLs in architecture documents.
- Keep `docs/Doc From Claude/` append-only; do not hand-edit `.nlm/` or `.claude/settings.local.json`.
- D-task flow: spec -> plan -> evidence -> report -> `M{n}-progress.md`; use numeric dotted IDs.

## Git, worktree, and A4000 contract

- GitLab `origin/l3-tdl` is integration source of truth; no GitHub remote is assumed.
- Never commit directly on `l3-tdl`. One concurrent task owns one short-lived branch, `.worktrees/<task>` checkout, and task-specific Compose project.
- Primary checkout is integration surface; feature development stays in task worktrees. Do not let threads share a branch/worktree without explicit handoff.
- `mass-l3-sil` owns stable demo runtime and normal demo ports. Feature experiments must not reuse, stop, or rebuild it.
- Exact environment and Compose values come from `scripts/a4000-env.sh` and active Compose files.
- Required promotion order: targeted tests -> local A4000 acceptance -> required adapter probe -> integrate -> push `origin/l3-tdl`.
- Canonical gate: `source scripts/a4000-env.sh && npm run sys:start && ./scripts/a4000-acceptance.sh`.
- Never use acceptance `--sync`, destructive reset, repository-wide overwrite, or unscoped Docker cleanup without explicit authorization.
- Full runtime, port, evidence, cleanup, and cache rules: `docs/Operations/a4000-runtime.md`.

## Project memory and CodeGraph delta

- Project NLM domains come from `.nlm/config.json`; do not duplicate notebook routing here.
- Follow global memory lifecycle; project wing is `MASS-L3`. If the active runtime lacks autosave, write one AAAK handoff before session end/compaction.
- After meaningful work append `handoff/workspace_log.md`; do not run retired `archive_to_headroom.py`.
- Every task worktree uses its own `.codegraph` index. Initialize/check it before relying on results.
- CodeGraph MCP `projectPath` and CLI cwd must equal the active worktree; never query a feature branch through another checkout index.

## Codex subagent routing

Primary agent is sole TDL Lead: it owns stage classification, routing, synthesis, and final authority. Routing must not be delegated to a router agent. No-chain rule: subagents must return findings to primary agent.

- Concrete failures: systematic debugging first, then SIL first-divergence evidence; use Spec Preflight only for missing/contradictory authority or contracts.
- Unique write owner: exactly one workspace writer per implementation task. Reviewers default to read-only; SIL must not edit production behavior.
- M5 must not self-approve M5-to-L4 executability; use independent GNC contract review.
- Approved M7 production implementation uses one fresh scoped writer in an isolated worktree; `tdl_m7_safety_reviewer` remains independent and read-only.
- Permission override may narrow access; expansion requires task-scoped write authorization, named paths, and reason. Reviewer independence cannot be overridden.
- Model and reasoning override must record reason. A Codex model override must use a model exposed by the current Codex runtime.

| Trigger / assignment | Role | Default |
|---|---|---|
| PRD, ConOps, ODD outcomes, operator workflow | `tdl_product_conops` | read-only |
| M1-M4 state, mission, CPA/TCPA, behavior | `tdl_decision_chain_engineer` | workspace-write |
| M6 rule, role, phase, direction, release | `tdl_colregs_m6_reasoner` | workspace-write |
| M5 Mid/BC-MPC, committed route, recovery | `tdl_m5_planner_engineer` | workspace-write |
| ROS2 message, topic, QoS, launch, bridge | `tdl_ros2_integration_engineer` | workspace-write |
| M8 HMI, replay, alerts, Runtime Console | `tdl_hmi_m8_frontend` | workspace-write |
| A4000, Docker, Compose, deployment gate | `tdl_devops_a4000_engineer` | workspace-write |
| Proven low-risk mechanical edit | `tdl_mechanical_implementer` | workspace-write |
| M5-to-L4 executability | `tdl_gnc_contract_reviewer` | read-only |
| M7, veto/MRM, degradation, fail-safe | `tdl_m7_safety_reviewer` | read-only |
| Production diff correctness/regression | `tdl_code_reviewer` | read-only |
| Trust boundary, secret, DDS/plugin exposure | `tdl_cyber_reviewer` | read-only |
| Certification claims and traceability | `tdl_cert_evidence_engineer` | read-only |
| SIL reproduction, RED/GREEN, first divergence | `tdl_sil_vv_engineer` | read-only |
| Spec preflight, contracts, readiness | `tdl_spec_architect` | read-only |

Conditional reviewers:

- M5/L4 route or recovery changes -> `tdl_gnc_contract_reviewer`.
- M5/M6/M7, ODD degradation, veto/MRM, or fail-safe -> `tdl_m7_safety_reviewer`.
- Non-mechanical production diff -> `tdl_code_reviewer`.
- Trust boundary, adapter, dependency, plugin, or Docker socket -> `tdl_cyber_reviewer`.
- Certification claim or traceability -> `tdl_cert_evidence_engineer`.
- Safety-critical behavior -> `tdl_sil_vv_engineer`.

### Mandatory task header

Before implementation state: affected modules/files; ROS2 topics/messages/IDL; ODD, COLREGs, and M5/M7 impacts; required tests/SIL scenarios/evidence.

### Completion contract

Return status (`DONE`, `DONE_WITH_CONCERNS`, `NEEDS_CONTEXT`, `BLOCKED`), changed paths, commands and exact results, evidence paths, interface/safety impacts, remaining risks, and escalation needs. Primary agent verifies evidence and alone declares completion.
