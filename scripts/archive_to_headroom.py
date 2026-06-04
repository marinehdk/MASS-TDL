#!/usr/bin/env python3
import os
import sys
import re
import sqlite3
import uuid
import hashlib
from datetime import datetime

DB_PATH = os.path.join(".headroom", "memory.db")
LOG_PATH = os.path.join("handoff", "workspace_log.md")

def get_db_connection(db_path):
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    return conn

def parse_log_entries(log_path):
    if not os.path.exists(log_path):
        print(f"Error: Log file not found at {log_path}")
        return []

    with open(log_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Split entries by horizontal rule or find headers
    sections = re.split(r"\n---\n", content)
    entries = []

    for section in sections:
        section = section.strip()
        if not section:
            continue
        
        # We only care about agent log sections starting with ## [Date] Agent: ...
        if section.startswith("## [") and "Agent:" in section:
            entries.append(section)

    return entries

def main():
    if not os.path.exists(DB_PATH):
        # Create directories if they do not exist
        os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
        print(f"Initializing empty headroom database at {DB_PATH}")
        conn = get_db_connection(DB_PATH)
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS memories (
                id TEXT PRIMARY KEY,
                content TEXT NOT NULL,
                user_id TEXT NOT NULL,
                session_id TEXT,
                agent_id TEXT,
                turn_id TEXT,
                created_at TEXT NOT NULL,
                valid_from TEXT NOT NULL,
                valid_until TEXT,
                category TEXT NOT NULL,
                importance REAL NOT NULL DEFAULT 0.5,
                supersedes TEXT,
                superseded_by TEXT,
                promoted_from TEXT,
                promotion_chain TEXT NOT NULL DEFAULT '[]',
                access_count INTEGER NOT NULL DEFAULT 0,
                last_accessed TEXT,
                entity_refs TEXT NOT NULL DEFAULT '[]',
                embedding BLOB,
                metadata TEXT NOT NULL DEFAULT '{}'
            );
            """
        )
        conn.commit()
    else:
        conn = get_db_connection(DB_PATH)

    entries = parse_log_entries(LOG_PATH)
    if not entries:
        print("No handoff entries found to import.")
        conn.close()
        return

    imported_count = 0
    skipped_count = 0

    user_id = os.environ.get("USER", "default")
    now_iso = datetime.utcnow().isoformat() + "Z"

    for entry in entries:
        # Generate a deterministic ID using the content hash to avoid duplicates
        content_hash = hashlib.sha256(entry.encode("utf-8")).hexdigest()
        mem_id = str(uuid.uuid5(uuid.NAMESPACE_DNS, content_hash))

        # Parse date and agent name for metadata
        date_match = re.search(r"##\s*\[(.*?)\]", entry)
        agent_match = re.search(r"Agent:\s*(.*)", entry.split("\n")[0])
        
        date_str = date_match.group(1) if date_match else ""
        agent_name = agent_match.group(1).strip() if agent_match else "Unknown"

        # Check if already exists
        cursor = conn.execute("SELECT id FROM memories WHERE id = ?", (mem_id,))
        if cursor.fetchone():
            skipped_count += 1
            continue

        # Format content clearly so vector embedding matching works well
        memory_content = (
            f"Developer Agent Handoff Log:\n"
            f"Date: {date_str}\n"
            f"Agent: {agent_name}\n"
            f"{entry}"
        )

        conn.execute(
            """
            INSERT OR REPLACE INTO memories (
                id, content, user_id, session_id, agent_id, turn_id,
                created_at, valid_from, valid_until,
                category, importance,
                supersedes, superseded_by, promoted_from, promotion_chain,
                access_count, last_accessed,
                entity_refs, embedding, metadata
            ) VALUES (
                ?, ?, ?, ?, NULL, NULL,
                ?, ?, NULL,
                ?, 0.8,
                NULL, NULL, NULL, '[]',
                0, NULL,
                '[]', NULL, '{}'
            )
            """,
            (
                mem_id, memory_content, user_id, f"handoff-{date_str}",
                now_iso, now_iso, "handoff"
            )
        )
        imported_count += 1

    conn.commit()
    conn.close()

    print(f"Successfully imported {imported_count} new entries, skipped {skipped_count} existing entries.")

if __name__ == "__main__":
    main()
