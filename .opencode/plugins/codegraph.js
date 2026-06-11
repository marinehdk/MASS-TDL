// CodeGraph OpenCode plugin
// Injects a code index reminder before bash tool calls when the index exists.
import { existsSync } from "fs";
import { join } from "path";

export const CodeGraphPlugin = async ({ directory }) => {
  let reminded = false;

  return {
    "tool.execute.before": async (input, output) => {
      if (reminded) return;
      if (!existsSync(join(directory, ".codegraph", "codegraph.db"))) return;

      if (input.tool === "bash") {
        output.args.command =
          'echo "[codegraph] code index at .codegraph/. Use CodeGraph MCP first; CLI fallback: \`codegraph query \"<symbol>\"\`, \`codegraph callers\`, \`codegraph callees\`, \`codegraph impact\`, \`codegraph status\`." && ' +
          output.args.command;
        reminded = true;
      }
    },
  };
};
