## branch / remote conventions

- 本地开发与 GitHub 以 `main` 作为主分支。
- A4000 上的 GitLab 仓库因权限限制，以 `l3-tdl` 作为主分支/同步目标。
- 涉及 A4000 部署、验收或 `scripts/a4000-acceptance.sh --sync` 时，目标分支使用 `l3-tdl`；不要把 A4000 GitLab 主线误判为 `main`。

## codegraph

This project uses CodeGraph as the code index. The index lives in `.codegraph/`; do not probe legacy graph-index paths.

Rules:
- For codebase questions, call `codegraph_explore` first. Use it for "how does X work", architecture, bug tracing, "where is X", and area surveys; one capped call usually returns the relevant source grouped by file.
- For focused follow-up, use `codegraph_search`, `codegraph_callers`, `codegraph_callees`, `codegraph_impact`, `codegraph_node`, `codegraph_files`, and `codegraph_status`.
- If the current Codex/Desktop thread does not expose the MCP tools, use the CodeGraph CLI fallback: `codegraph query`, `codegraph callers`, `codegraph callees`, `codegraph impact`, `codegraph files`, and `codegraph status`.
- Avoid broad grep or full-file reads before CodeGraph gives coordinates. Use raw source reads only to confirm a specific detail CodeGraph did not cover.
- The CodeGraph watcher normally syncs writes in about 1 second. No manual update is needed after edits; if `codegraph status .` shows pending files or the watcher is unavailable, run `codegraph sync .`.
