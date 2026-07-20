# AGENTS.md Compaction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Do not delegate this documentation-only change.

**Goal:** Reduce project `AGENTS.md` prompt cost while preserving MASS-L3 safety invariants, executable operating rules, and all validated Codex routing contracts.

**Architecture:** Treat global `~/.codex/AGENTS.md` as generic workflow authority and project `AGENTS.md` as a project-delta contract. Move volatile A4000 commands and values into one canonical repo runbook; keep only hard safety/branch/gate rules and concise links in `AGENTS.md`.

**Tech Stack:** Markdown, Bash/Python validation scripts, Git.

## Global Constraints

- Work only in `/home/marine.huang/Code/mass-l3/.worktrees/agents-md-compact` on `codex/agents-md-compact`.
- Preserve project architecture invariants and COLREGs full-chain debugging discipline.
- Preserve a Markdown heading identifying `Codex` routing and a positive route for all 15 specialist roles.
- Keep exact validator tokens for stage classification, unique write owner, permission/model override, no-chain, task header, completion contract, systematic debugging, and SIL first divergence.
- Do not modify production code, ROS2 interfaces, solver behavior, safety thresholds, or runtime scripts.

---

### Task 1: Create canonical A4000 runtime runbook

**Files:**
- Create: `docs/Operations/a4000-runtime.md`

**Interfaces:**
- Consumes: `scripts/a4000-env.sh`, `scripts/a4000-acceptance.sh`, compose files.
- Produces: stable operational reference linked from `AGENTS.md`.

- [x] **Step 1: Write runbook**

Include source-of-truth branch, worktree isolation, demo/feature compose ownership, canonical gate, reserved ports, external adapter probe, scoped cleanup, and cache rules. State scripts/compose files own exact environment values.

- [x] **Step 2: Verify referenced paths**

Run:

```bash
test -f scripts/a4000-env.sh
test -f scripts/a4000-acceptance.sh
test -f docker-compose.a4000.yml
test -f docker-compose.plugins.yml
```

Expected: exit 0.

### Task 2: Compact project AGENTS.md

**Files:**
- Modify: `AGENTS.md`

**Interfaces:**
- Consumes: global generic workflow rules, A4000 runbook, `.codex/agents/*.toml`.
- Produces: concise project-delta contract accepted by the routing validator.

- [x] **Step 1: Replace repeated operating prose**

Keep seven sections only:

```text
Project scope and invariants
COLREGs full-chain debugging
Design authority
Git, worktree, and A4000 contract
Project memory and CodeGraph delta
Codex subagent routing
Mandatory task header and completion contract
```

- [x] **Step 2: Preserve routing validator contract**

Run before final verification:

```bash
python3 scripts/validate_codex_tdl_agents.py
```

Expected: `OK: validated 15 TDL agent definitions and routing contracts`.

- [x] **Step 3: Verify compaction and Markdown integrity**

Run:

```bash
test "$(wc -l < AGENTS.md)" -le 150
rg -n "docs/Operations/a4000-runtime.md|MASS-L3|Codex subagent routing" AGENTS.md
git diff --check
```

Expected: all commands exit 0; `AGENTS.md` is at most 150 lines.

### Task 3: Final review

**Files:**
- Review: `AGENTS.md`
- Review: `docs/Operations/a4000-runtime.md`
- Review: `docs/superpowers/plans/2026-07-20-agents-md-compaction.md`

**Interfaces:**
- Consumes: completed documentation diff.
- Produces: evidence-backed handoff without commit, merge, or push.

- [x] **Step 1: Inspect scope**

Run:

```bash
git status --short
git diff --stat
git diff -- AGENTS.md docs/Operations/a4000-runtime.md
```

Expected: only the plan, `AGENTS.md`, and runtime runbook changed.

- [x] **Step 2: Run fresh final verification**

Run:

```bash
python3 scripts/validate_codex_tdl_agents.py
python3 scripts/validate_codex_tdl_agents.py --self-test
git diff --check
```

Expected: routing validation OK, 142 adversarial fixtures PASS, no whitespace errors.
