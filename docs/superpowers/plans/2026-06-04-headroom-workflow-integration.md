# Headroom and Graphify Workflow Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Configure and unify Headroom (API proxy + MCP) and Graphify into a local developer workflow that synchronizes conversation memory, saves tokens, and enables seamless handoffs across Claude Code CLI, OpenCode CLI, Claude Desktop App, and Antigravity.

**Architecture:** Use a background `headroom proxy` for token compression on CLI calls, integrate the Headroom MCP server into Claude Desktop for tool-level memory access, index structures with Graphify (AST), and synchronize active session threads via a Markdown handoff ledger (`docs/workspace_log.md`).

**Tech Stack:** Zsh, Headroom CLI, SQLite, Graphify CLI, MCP (Model Context Protocol).

---

### Task 1: Shell Configurations & Aliases

**Files:**
- Modify: `~/.zshrc` (or create backup if editing)

- [ ] **Step 1: Check ~/.zshrc existence and write backup**

  Run: `cp ~/.zshrc ~/.zshrc.headroom.bak`
  Expected: Backup created successfully.

- [ ] **Step 2: Append workflow aliases to ~/.zshrc**

  Append the following lines to the end of `~/.zshrc`:
  ```bash
  # --- Headroom & Coding Agent Workflow Aliases ---
  # Claude Code CLI (wrapped with headroom)
  alias ccode="headroom wrap claude"

  # OpenCode CLI (routed via headroom proxy)
  alias opencode="export OPENAI_BASE_URL=http://127.0.0.1:8787/v1 && opencode"
  ```

- [ ] **Step 3: Source shell configuration and verify aliases**

  Run: `source ~/.zshrc && alias ccode && alias opencode`
  Expected: Aliases printed out matching the configuration.

---

### Task 2: Inject Handoff & Token-Saving Rules into CLAUDE.md

**Files:**
- Modify: `/Users/marine/Code/MASS-L3-Tactical Layer/CLAUDE.md`

- [ ] **Step 1: Append coordination rules section to CLAUDE.md**

  Append the following content at the bottom of the `CLAUDE.md` file:
  ```markdown

  ## 跨客户端开发协同与 Token 节约协议

  ### 1. 智能会话接力规范
  - **启动时（上下文链回溯）**：
    在会话开始执行任何实质开发或测试操作前，你必须首先读取并检索 `docs/workspace_log.md`。请根据当前用户提出的开发目标进行关键词或语义检索，**自动寻找与当前开发模块最相关的前置日志记录**，从而拼接出完整的上下文链路，杜绝信息丢失。

  - **结束时（统一格式日志记录）**：
    在会话结束或回答用户任务完成前，你必须向 `docs/workspace_log.md` 底部追加一条格式严格对齐的开发记录（或模仿已有的日志样式填写）。标准格式规范如下：

    ## [YYYY-MM-DD HH:MM] Agent: <客户端名称，如 Claude Code CLI>
    - **Git Commit**: `<Commit Hash>` (branch: `<当前分支名>`)
    - **Headroom Session**: `<Session ID>` (若有，记录本次会话的代理会话标识)
    - **Headroom Refs**: `[ref_<Hash>]` (记录本次会话中重要的大日志或大文本引用哈希)
    - **任务目标 (Goal)**: <简短的一句话描述本次会话的任务目标>
    - **核心改动 (Actions)**:
      - `[修改文件相对路径](file:///absolute/path/to/file)`: <简要说明在此文件做了什么改动>
    - **当前状态 (Status)**: <运行结果，如：单元测试全部通过 / 重构通过，待进行链路测试>
    - **接力指示 (Hand-off Context)**: <留给下一个接棒 Agent 的具体执行指令 and 上下文>

  ### 2. Token 节约与代码搜索规范
  - **禁止暴力搜索**：严禁在未做定位的情况下，使用大范围 `grep` 或读取大量整个源代码文件。
  - **优先使用 Graphify**：当需要定位代码、分析类继承关系或方法调用链路时，优先在终端运行 `graphify query "<问题>"`，仅根据返回的精确代码坐标和关系链去读取特定文件范围。
  - **静态图谱更新**：每次修改代码且测试通过后，你必须在终端运行 `graphify update .` 刷新本地静态图谱（本地计算，0 Token 成本）。
  ```

- [ ] **Step 2: Commit CLAUDE.md changes**

  Run: `git add CLAUDE.md && git commit -m "docs: add headroom and graphify handoff rules to CLAUDE.md"`
  Expected: Commit succeeds.

---

### Task 3: Initialize Handoff Ledger File

**Files:**
- Create: `/Users/marine/Code/MASS-L3-Tactical Layer/docs/workspace_log.md`

- [ ] **Step 1: Write initial Markdown structure to docs/workspace_log.md**

  Write the following content:
  ```markdown
  # Project Development & Agent Handoff Log

  This log coordinates task handoffs between different development interfaces (Claude Desktop, Claude Code CLI, OpenCode, Antigravity) to prevent context loss.

  ---
  ```

- [ ] **Step 2: Add and commit the handoff ledger to Git**

  Run: `git add docs/workspace_log.md && git commit -m "docs: initialize shared agent handoff log"`
  Expected: Commit succeeds.

---

### Task 4: Multi-Agent E2E Verification & Hand-off Testing

This task performs a real-world multi-turn developer hand-off simulation across three applications (Claude Code CLI, Claude Desktop Code Tab, and OpenCode CLI) on a dummy feature branch.

**Files:**
- Create: `docs/test_headroom_feature.md` (Temporary test file)
- Modify: `docs/workspace_log.md` (Handoff verification updates)

- [ ] **Step 1: Setup a temporary git branch for integration test**

  Run: `git checkout -b test/headroom-integration-flow`
  Expected: Switched to a new branch `test/headroom-integration-flow`.

- [ ] **Step 2: Start headroom proxy in the background**

  Run: `headroom proxy` in a separate terminal.
  Ensure it starts successfully (defaulting to port `8787`). Access `http://127.0.0.1:8787/dashboard` to verify the visual UI is up.

- [ ] **Step 3: Run Turn 1 in Claude Code CLI (Terminal)**

  Run: `ccode`
  In the session, instruct Claude Code:
  > "Please create a test file `docs/test_headroom_feature.md` containing a list of 10 mock server names. When done, write a handoff entry to `docs/workspace_log.md`按照CLAUDE.md的日志规范格式."

  Verify:
  1. Check `docs/test_headroom_feature.md` is created.
  2. Verify that Claude Code successfully appends a log entry to `docs/workspace_log.md`.
  3. Exit the `ccode` session.

- [ ] **Step 4: Run Turn 2 in Claude Desktop (GUI App)**

  Open the **Claude Desktop App** (normally via Dock).
  1. Navigate to the `Code` mode tab.
  2. Start a session in the `MASS-L3-Tactical Layer` project (specifically branch `test/headroom-integration-flow`).
  3. Input the following prompt:
     > "Please read the handoff log in `docs/workspace_log.md` to see what the previous agent did, then read `docs/test_headroom_feature.md` and sort the 10 mock server names alphabetically. Once done, append a handoff log entry to `docs/workspace_log.md` specifying that the next step is to commit the sorted list using OpenCode."

  Verify:
  1. The Desktop agent correctly locates and reads the handoff log entry created by the terminal CLI in Step 3.
  2. The Desktop agent successfully reads `docs/test_headroom_feature.md`, sorts it, and writes the sorted output back.
  3. The Desktop agent appends its handoff log entry to `docs/workspace_log.md` with the unified layout.

- [ ] **Step 5: Run Turn 3 in OpenCode CLI (Terminal)**

  Run: `opencode` (which will route through `http://127.0.0.1:8787/v1` via alias).
  In the session, instruct OpenCode:
  > "Search `docs/workspace_log.md` to see what needs to be done. Commit the sorted list `docs/test_headroom_feature.md` to git, then append the final handoff log entry specifying that the verification test is successfully completed."

  Verify:
  1. OpenCode successfully reads `docs/workspace_log.md`, finds the task indicating it needs to commit the changes.
  2. OpenCode runs the git commands to add and commit `docs/test_headroom_feature.md` (which routes through Headroom proxy and is cached).
  3. OpenCode appends the final log entry to `docs/workspace_log.md`.
  4. Exit `opencode`.

- [ ] **Step 6: Cleanup the integration test branch**

  Verify the complete log history in `docs/workspace_log.md`.
  Run the cleanup command:
  ```bash
  git checkout main
  git branch -D test/headroom-integration-flow
  rm docs/test_headroom_feature.md
  # Restore the log file if you don't want the mock entries in main branch
  git checkout -- docs/workspace_log.md
  ```
  Expected: Cleaned up and returned to the main branch safely.
