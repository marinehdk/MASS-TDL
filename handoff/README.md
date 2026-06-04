# Handoff & Session Handoff Log Directory

This directory manages the local project handoff logs and related documentation to preserve conversation context across multiple agent clients (**Claude Code CLI**, **Claude Desktop**, **OpenCode CLI**, and **Antigravity**).

## Contents

- `workspace_log.md`: The active transaction log where agents write their session summaries at the end of each session.
- `README.md`: This configuration and guide document.

---

## Instructions for Agents

### 1. On Startup (Session Recovery)
Before executing any development or test task, agents must read `handoff/workspace_log.md` and query the most relevant preceding transaction using keyword/semantic mapping to establish a continuous context.

### 2. On Shutdown (Handoff Log Entry)
Before completing the session or answering the user, the agent must append a structured entry to `handoff/workspace_log.md` matching the following template:

```markdown
## [YYYY-MM-DD HH:MM] Agent: <Client Name>
- **Git Commit**: `<Commit Hash>` (branch: `<Branch Name>`)
- **Headroom Session**: `<Session ID if applicable>`
- **Headroom Refs**: `[ref_<Hash> if applicable]`
- **任务目标 (Goal)**: <Summary of goal>
- **核心改动 (Actions)**:
  - `[file basename](file:///absolute/path/to/file)`: <Description of changes>
- **当前状态 (Status)**: <Status description>
- **接力指示 (Hand-off Context)**: <Instructions for next agent>
```

---

## Workflow Reference

1. **Claude Code CLI** (wrapped via Headroom: `ccode`)
2. **Claude Desktop** (MCP tools enabled)
3. **OpenCode CLI** (Headroom routed: `opencode`)
4. **Antigravity** (IDE environment)
