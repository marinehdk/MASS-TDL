# TDL Codex Subagent Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the approved MASS-TDL specialist team to Codex custom agents and connect repository routing, Spec Preflight, permissions, model defaults, and validation.

**Architecture:** Keep the primary Codex agent as the only TDL Lead and router. Store durable routing and independence rules in `AGENTS.md`, role-local defaults and boundaries in `.codex/agents/*.toml`, and pre-Spec routing output in the personal `spec-preflight` skill. Validate the three surfaces with one deterministic contract test, then forward-test representative architecture and scenario routes with read-only agents.

**Tech Stack:** Markdown, TOML, Python 3.11+ `tomllib`, Codex custom agents, Superpowers skills.

## Global Constraints

- Preserve all pre-existing user changes in the primary checkout; work only in `/home/marine.huang/Code/mass-l3/.worktrees/tdl-subagent-routing` plus the explicitly authorized personal skill `/home/marine.huang/.codex/skills/spec-preflight`.
- Do not retain `tdl_router_architect`; routing belongs to the primary agent through `AGENTS.md` and `spec-preflight`.
- Add `tdl_gnc_contract_reviewer` as an independent read-only reviewer.
- Use `gpt-5.6-sol` as the verified role-default model; use `medium`, `high`, or `xhigh` per role and allow task dispatch to override.
- Reviewer roles remain `read-only`. `tdl_sil_vv_engineer` defaults to `read-only`; explicit test/scenario-authoring assignments may override it to `workspace-write` but never authorize production behavior edits.
- Write-capable roles receive `workspace-write`, narrow ownership, prohibited changes, and mandatory completion evidence.
- Use underscore agent names inside TOML and routing; use hyphenated filenames under `.codex/agents/`.
- NLM route remains `nlm-ask -> nlm-research --depth fast -> one sequential deep job only when justified -> official web fallback`.
- Do not depend on retired ZCode-only skills `tdl-code-review`, `tdl-sil-verify`, or `tdl-avoidance-debug`.
- Subagents return to the primary agent; no free chain delegation or majority-vote decisions.

---

### Task 1: Add the routing contract validator and establish RED

**Files:**
- Create: `scripts/validate_codex_tdl_agents.py`
- Test: `scripts/validate_codex_tdl_agents.py`

**Interfaces:**
- Consumes: repository root, `.codex/agents/*.toml`, `AGENTS.md`, optional `--skill-dir`.
- Produces: deterministic exit code and per-contract diagnostics.

- [x] **Step 1: Implement structural assertions**

Validate expected role set, unique TOML names, required keys, allowed model/effort/sandbox values, role permission matrix, mandatory instruction phrases, absence of obsolete router and ZCode-only skills, `AGENTS.md` route coverage, and Spec Preflight routing-output coverage.

- [x] **Step 2: Run against the incomplete baseline**

Run:

```bash
python3 scripts/validate_codex_tdl_agents.py --skill-dir /home/marine.huang/.codex/skills/spec-preflight
```

Expected: non-zero exit and diagnostics for missing agents/routing.

### Task 2: Add all Codex agent definitions and repository routing

**Files:**
- Create: `.codex/agents/tdl-product-conops.toml`
- Create: `.codex/agents/tdl-decision-chain-engineer.toml`
- Create: `.codex/agents/tdl-colregs-m6-reasoner.toml`
- Create: `.codex/agents/tdl-m5-planner-engineer.toml`
- Create: `.codex/agents/tdl-ros2-integration-engineer.toml`
- Create: `.codex/agents/tdl-hmi-m8-frontend.toml`
- Create: `.codex/agents/tdl-devops-a4000-engineer.toml`
- Create: `.codex/agents/tdl-mechanical-implementer.toml`
- Create: `.codex/agents/tdl-gnc-contract-reviewer.toml`
- Create: `.codex/agents/tdl-m7-safety-reviewer.toml`
- Create: `.codex/agents/tdl-code-reviewer.toml`
- Create: `.codex/agents/tdl-cyber-reviewer.toml`
- Create: `.codex/agents/tdl-cert-evidence-engineer.toml`
- Create: `.codex/agents/tdl-sil-vv-engineer.toml`
- Create: `.codex/agents/tdl-spec-architect.toml`
- Modify: `AGENTS.md`

**Interfaces:**
- Consumes: approved stage-by-domain team model and existing ZCode role knowledge.
- Produces: 15 discoverable Codex roles plus primary-agent routing policy.

- [x] **Step 1: Define the 15 narrow roles**

Each TOML must include `name`, `description`, `model`, `model_reasoning_effort`, `sandbox_mode`, and `developer_instructions`. Preserve domain boundaries, explicit non-responsibilities, evidence rules, write scope, escalation, and a concise completion contract.

- [x] **Step 2: Replace ZCode routing with Codex routing**

Add stage classification, unique write owner, conditional independent reviewers, permissions/model override policy, stage/domain tables, no-chain rule, mandatory task header, and completion contract. Route M5-to-L4 executability to the independent GNC reviewer. Route concrete failures through systematic debugging and SIL first-divergence analysis before design escalation.

- [x] **Step 3: Run TOML parse and routing validation**

Run:

```bash
python3 -c 'import pathlib,tomllib; [tomllib.loads(p.read_text()) for p in pathlib.Path(".codex/agents").glob("*.toml")]'
python3 scripts/validate_codex_tdl_agents.py --skill-dir /home/marine.huang/.codex/skills/spec-preflight
```

Expected: TOML parse passes; validator still fails only if the personal skill routing handoff is incomplete.

### Task 3: Connect Spec Preflight to the role router

**Files:**
- Modify: `/home/marine.huang/.codex/skills/spec-preflight/SKILL.md`
- Modify: `/home/marine.huang/.codex/skills/spec-preflight/references/preflight-brief-template.md`
- Validate: `/home/marine.huang/.codex/skills/spec-preflight/`

**Interfaces:**
- Consumes: Preflight classification, affected authority boundaries, unresolved evidence gaps.
- Produces: pre-Spec `Required Agent Routing` with fixed `PRE_SPEC_DISCOVERY`, `MAIN_AGENT`, `Write authorization: NONE`, bounded read-only evidence specialists, model/reasoning overrides, reasons, and evidence.

- [x] **Step 1: Add routing synthesis to the mandatory workflow**

Require fixed pre-Spec stage, main-agent ownership, read-only evidence specialists, no write authorization, evidence-oriented model/reasoning overrides, and evidence contract. Preserve final authority in the primary agent; leave implementation/debugging/deployment routing to `AGENTS.md`.

- [x] **Step 2: Add the routing block to the brief template**

Add fixed fields and reject incompatible combinations: any preflight write permission, M5 self-approval of GNC executability, SIL production/scenario edits, reviewer write mode, later-stage owner suggestions, or router-agent delegation.

- [x] **Step 3: Validate the personal skill**

Run:

```bash
python3 /home/marine.huang/.codex/skills/.system/skill-creator/scripts/quick_validate.py /home/marine.huang/.codex/skills/spec-preflight
python3 scripts/validate_codex_tdl_agents.py --skill-dir /home/marine.huang/.codex/skills/spec-preflight
```

Expected: both commands exit 0.

### Task 4: Forward-test representative routes and complete verification

**Files:**
- Read: `AGENTS.md`
- Read: `.codex/agents/*.toml`
- Read: `/home/marine.huang/.codex/skills/spec-preflight/**`

**Interfaces:**
- Consumes: raw user-style architecture, scenario/debugging, and low-risk mechanical requests.
- Produces: route decisions demonstrating correct owner/reviewer/permission/model/evidence selection.

- [x] **Step 1: Architecture pressure test**

Ask a fresh read-only agent to route an M5/M7/L4 committed-route architecture change. Require no implementation. Expected: Spec Preflight, `tdl_spec_architect`, bounded read-only M5/GNC/M7/SIL evidence requests, and no future implementation owner or write permission inside the Preflight Brief. `AGENTS.md` owns later routing after approved Spec/Plan.

- [x] **Step 2: Scenario pressure test**

Ask a fresh read-only agent to route a Rule 14 RED scenario where the user proposes relaxing a threshold. Expected: systematic debugging and SIL first divergence; conditional M6/M5/GNC routing; no immediate Spec or threshold edit.

- [x] **Step 3: Mechanical pressure test**

Ask a fresh read-only agent to route a one-file typo/formatting change. Expected: mechanical implementer only, no architecture panel, scoped write permission, cheap reasoning default.

- [x] **Step 4: Run final deterministic checks**

Run:

```bash
python3 scripts/validate_codex_tdl_agents.py --skill-dir /home/marine.huang/.codex/skills/spec-preflight
python3 -m compileall -q scripts/validate_codex_tdl_agents.py
git diff --check
git status --short
```

Expected: validation passes, compile passes, diff check passes, and status lists only task-owned files.
