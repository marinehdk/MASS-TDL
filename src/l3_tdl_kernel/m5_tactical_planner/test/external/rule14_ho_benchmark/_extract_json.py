#!/usr/bin/env python3
"""Extract the last top-level JSON object from a text stream.

The Rule14 benchmark runner writes a single JSON object to stdout, but the
IPOPT library prints a startup banner to stdout on first call (CasADi dlopen)
that PRECEDES the JSON. This helper finds the JSON object (the substring
starting at the LAST line that begins with optional-whitespace + '{' and
extending to the matching closing '}' at the same bracket depth) and writes
it to stdout.

Robust to: IPOPT banner text, spdlog warnings (those go to stderr, but be
defensive), and any trailing whitespace.
"""
import sys


def extract_json(text: str) -> str:
    # Find the FIRST TOP-LEVEL '{' — a line that begins with '{' at column 0
    # (no indentation). The IPOPT banner and spdlog warnings precede it and
    # never start a line with an unindented '{'. Nested JSON object lines
    # (e.g. trajectory entries `    {"k": 0, ...}`) are indented, so they are
    # skipped by the column-0 requirement.
    lines = text.splitlines()
    start_idx = None
    for i, line in enumerate(lines):
        if line.startswith("{"):
            start_idx = i
            break
    if start_idx is None:
        raise ValueError("no top-level JSON object found "
                         "(no line starts with '{' at column 0)")
    # Walk forward tracking brace depth to find the matching close.
    depth = 0
    out = []
    in_string = False
    escape = False
    for line in lines[start_idx:]:
        out.append(line)
        for ch in line:
            if escape:
                escape = False
                continue
            if ch == "\\":
                escape = True
                continue
            if ch == '"':
                in_string = not in_string
                continue
            if in_string:
                continue
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return "\n".join(out) + "\n"
    raise ValueError("unbalanced braces: JSON object did not close")


def main():
    text = open(sys.argv[1]).read()
    sys.stdout.write(extract_json(text))


if __name__ == "__main__":
    main()
