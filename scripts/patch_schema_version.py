#!/usr/bin/env python3
"""Insert schema_version field into top-level ROS2 .msg files.

Usage: python3 scripts/patch_schema_version.py <msg_file> [<msg_file> ...]

Inserts: string schema_version  # default: "v1.1.2"
Position: after the last leading comment line, before the first field.
"""
import sys
from pathlib import Path


def patch_msg_file(path: Path) -> bool:
    """Return True if file was modified, False if already patched."""
    text = path.read_text(encoding="utf-8")
    if 'schema_version' in text:
        print(f"  SKIP (already patched): {path}")
        return False

    lines = text.splitlines(keepends=True)
    # Find insertion point: first line that is not a comment and not blank
    insert_at = len(lines)  # fallback: append
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped and not stripped.startswith('#'):
            insert_at = i
            break

    schema_line = 'string schema_version  # default: "v1.1.2"\n'
    lines.insert(insert_at, schema_line)
    path.write_text(''.join(lines), encoding="utf-8")
    print(f"  PATCHED: {path} (inserted at line {insert_at + 1})")
    return True


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: patch_schema_version.py <msg_file> [...]")
        sys.exit(1)

    patched = 0
    for arg in sys.argv[1:]:
        p = Path(arg)
        if not p.exists():
            print(f"  ERROR: {p} not found", file=sys.stderr)
            sys.exit(1)
        if patch_msg_file(p):
            patched += 1

    print(f"\nDone. {patched}/{len(sys.argv) - 1} files patched.")


if __name__ == "__main__":
    main()
