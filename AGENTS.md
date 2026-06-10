## branch / remote conventions

- 本地开发与 GitHub 以 `main` 作为主分支。
- A4000 上的 GitLab 仓库因权限限制，以 `l3-tdl` 作为主分支/同步目标。
- 涉及 A4000 部署、验收或 `scripts/a4000-acceptance.sh --sync` 时，目标分支使用 `l3-tdl`；不要把 A4000 GitLab 主线误判为 `main`。

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, invoke the `skill` tool with `skill: "graphify"` before doing anything else.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).

## cross-app handoff relay

OpenCode has no Stop / SessionStart / PreCompact hook events (only plugin
modules), so relay is **half-automatic** vs Claude Code's fully-automatic. The
pieces:

- **At session start** — before substantial work, prepend your first message
  with the output of `python3 ~/.claude/hooks/oc-recall.py --brief` (or the
  full snapshot, no flag, if you want DEBUG_STATE.md surface too). This loads
  where the prior session left off.
- **At session end** — run `python3 ~/.claude/hooks/oc-save.py` (auto-discovers
  the most-recent opencode session for the cwd). It overwrites
  `handoff/.live_state.md` and stages a clean .md under
  `~/.claude/hooks/.oc_sessions/` for MemPalace ingestion.
- **Semantic recall** — `mempalace search "<kw>"` works across Claude Code,
  Claude Desktop-Code, OpenCode, and Antigravity (all routed to the same
  MemPalace). Use that for "did we discuss X" questions.
- **Mid-debug switch** — run `/handoff-debug` in Claude Code first; it writes
  `DEBUG_STATE.md` that oc-recall will surface when you resume here.
- **Provider switch hard guard** — before switching provider via CC-Switch,
  run `~/.claude/hooks/provider-guard.py --model "<name>"` to confirm the
  model name actually resolves (catches typos like the 06-05 `MiniMax-M3[1M]`
  incident before they break a live session).

For deeper context, see `~/.claude/hooks/README-handoff.md`. Headroom is **only
a compression proxy** here (:8787 in `opencode.json`); it is not a memory layer.
