# Design Spec: Unified AI Developer Workspace Workflow
**Date**: 2026-06-04
**Topic**: Local Workflow Integration for Headroom, Graphify, and Cross-Agent Memory Synchronization
**Status**: Draft (Awaiting User Review)

---

## 1. Goal & Context

The goal is to establish a unified local AI agent development workflow in the `/Users/marine/Code/MASS-L3-Tactical Layer` workspace. 

As developers switch between multiple AI interfaces due to subscription limits or feature preferences—including **Claude Code CLI** (terminal), **OpenCode CLI** (terminal), **Claude Desktop App** (GUI Code tab), and **Antigravity** (IDE plugin)—they face two primary challenges:
1. **Context Loss**: Scattered conversation histories and agent memories across apps.
2. **Token & Cost Waste**: Redundant workspace code reading and bloated tool search results (e.g., broad `grep` outputs).

This specification coordinates **Headroom** (API proxy & SQLite memory db) and **Graphify** (static AST knowledge graph) into a cohesive system to minimize token usage, maintain conversational continuity, and sync development threads across all active agents.

---

## 2. Workspace Architecture

The system operates across three logical layers:

```
           ┌──────────────────────────────────────────────┐
           │              Your Mac Workspace              │
           │  /Users/marine/Code/MASS-L3-Tactical Layer   │
           └──────────────────────┬───────────────────────┘
                                  │
      ┌───────────────────────────┼───────────────────────────┐
      ▼                           ▼                           ▼
[Claude Code CLI]          [OpenCode CLI]             [Claude Desktop App]
  (via Terminal)            (via Terminal)             (GUI local agent)
      │                           │                           │
      │ (wrap / ANTHROPIC_URL)    │ (OPENAI_BASE_URL)         │ (MCP Protocol)
      ▼                           ▼                           ▼
 ┌─────────────────────────────────────────────────────────────┐
 │                Headroom Local Proxy & MCP                   │
 │                     (127.0.0.1:8787)                        │
 └────────────────────────────┬────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
            [SQLite Shared DB]    [Graphify Graph]
           (.headroom/memory.db)  (graphify-out/graph.json)
```

1. **API Interception & Token Compactor (Headroom Proxy)**:
   A background daemon running on `127.0.0.1:8787` that intercepts HTTP requests from terminal CLI agents. It compresses file structures and command outputs into lightweight hashes before transmitting them to the LLM.
2. **Memory Sharing (Headroom MCP)**:
   Registered inside Claude Desktop and Claude Code configuration registries. Enables agents to invoke `headroom_retrieve` or `headroom_compress` to recall the raw content of cached references directly from the local SQLite database.
3. **AST-based Indexing (Graphify)**:
   A zero-cost codebase graph that agents query to locate functions and classes without performing brute-force file tree scans or wide-range text greps.
4. **Handoff Ledger (`docs/workspace_log.md`)**:
   A human-and-agent-readable journal that bridges high-level progress details, Git commits, and Headroom session markers.

---

## 3. Client & Terminal Configurations

The following configurations must be set up in the Zsh environment (`~/.zshrc`) to route agents through Headroom:

### A. Claude Code CLI
Wrapped to start the local proxy automatically and route Anthropic API traffic:
```bash
alias ccode="headroom wrap claude"
```

### B. OpenCode CLI
Configured to forward OpenAI-compatible requests through the Headroom proxy:
```bash
alias opencode="export OPENAI_BASE_URL=http://127.0.0.1:8787/v1 && opencode"
```

### C. Claude Desktop App
Configured in `~/Library/Application Support/Claude/claude_desktop_config.json` to load the Headroom MCP server (installed globally at `/Users/marine/.local/bin/headroom`):
```json
{
  "mcpServers": {
    "headroom": {
      "command": "/Users/marine/.local/bin/headroom",
      "args": [
        "mcp",
        "serve"
      ]
    }
  }
}
```
*Note: The Desktop App's Chat mode runs directly via Web Sockets and bypasses the proxy. However, the local agentic "Code" tab will invoke the Headroom MCP tools to interact with the database.*

---

## 4. Coordination Rules (`CLAUDE.md`)

To enforce the handoff and search policies, the following instructions will be added to the project's `CLAUDE.md` file:

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

---

## 5. Handoff Ledger Structure (`docs/workspace_log.md`)

A new file, `docs/workspace_log.md`, will be created with the following initial structure:

```markdown
# Project Development & Agent Handoff Log

This log coordinates task handoffs between different development interfaces (Claude Desktop, Claude Code CLI, OpenCode, Antigravity) to prevent context loss.

---
```

---

## 6. Verification Plan

### A. Headroom Proxy & Memory Integration
1. Run `headroom proxy` in a background terminal.
2. Launch terminal Claude Code using the `ccode` alias.
3. Perform a large file read command or command execution.
4. Verify that the output is compressed into a reference token (e.g. `[headroom_ref: <hash>]`).
5. Run `headroom memory list --db-path ./headroom_memory.db` and verify a new memory record was created.

### B. Cross-Agent MCP Handshake
1. Open Claude Desktop, navigate to the `Code` mode, and run a query.
2. Verify that Claude Desktop accesses the registered `headroom` MCP tools without error.
3. Check the headroom logs to confirm the MCP server is serving on-demand retrieval requests.

### C. Workspace Log Continuation
1. Run a session in terminal `ccode`, write code changes, and verify it automatically appends a formatted entry to `docs/workspace_log.md` upon completion.
2. Start a session in `opencode` or `claude-desktop`, verify the agent parses the previous entry, and asks you to proceed with the specific next step outlined in the log.
