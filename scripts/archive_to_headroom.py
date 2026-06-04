#!/usr/bin/env python3
import os
import sys
import re
import json
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

    sections = re.split(r"\n---\n", content)
    entries = []

    for section in sections:
        section = section.strip()
        if not section:
            continue
        
        if section.startswith("## [") and "Agent:" in section:
            entries.append(section)

    return entries

def get_latest_convo_dir():
    brain_dir = os.path.expanduser("~/.gemini/antigravity/brain")
    if not os.path.exists(brain_dir):
        return None, None
    
    subdirs = []
    for d in os.listdir(brain_dir):
        path = os.path.join(brain_dir, d)
        if os.path.isdir(path):
            log_file = os.path.join(path, ".system_generated", "logs", "transcript.jsonl")
            if os.path.exists(log_file):
                subdirs.append((path, d, os.path.getmtime(log_file)))
                
    if not subdirs:
        return None, None
        
    subdirs.sort(key=lambda x: x[2], reverse=True)
    return subdirs[0][0], subdirs[0][1]

def parse_transcript_turns(transcript_path):
    turns = []
    if not os.path.exists(transcript_path):
        return turns

    current_user = None
    with open(transcript_path, "r", encoding="utf-8") as f:
        for line in f:
            try:
                data = json.loads(line)
                step_type = data.get("type")
                content = data.get("content", "")
                
                if step_type == "USER_INPUT":
                    match = re.search(r"<USER_REQUEST>(.*?)</USER_REQUEST>", content, re.DOTALL)
                    if match:
                        current_user = match.group(1).strip()
                    else:
                        current_user = content.strip()
                elif step_type == "PLANNER_RESPONSE" and current_user:
                    # Clean the assistant response if it has thinking blocks
                    assistant_text = content.strip()
                    turns.append({
                        "user": current_user,
                        "assistant": assistant_text,
                        "timestamp": data.get("created_at")
                    })
                    current_user = None
            except Exception:
                continue
    return turns

def main():
    if not os.path.exists(DB_PATH):
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

    # 1. Sync high-level handoff logs
    entries = parse_log_entries(LOG_PATH)
    handoff_imported = 0
    handoff_skipped = 0
    user_id = os.environ.get("USER", "default")
    now_iso = datetime.utcnow().isoformat() + "Z"

    for entry in entries:
        content_hash = hashlib.sha256(entry.encode("utf-8")).hexdigest()
        mem_id = str(uuid.uuid5(uuid.NAMESPACE_DNS, content_hash))

        date_match = re.search(r"##\s*\[(.*?)\]", entry)
        agent_match = re.search(r"Agent:\s*(.*)", entry.split("\n")[0])
        
        date_str = date_match.group(1) if date_match else ""
        agent_name = agent_match.group(1).strip() if agent_match else "Unknown"

        cursor = conn.execute("SELECT id FROM memories WHERE id = ?", (mem_id,))
        if cursor.fetchone():
            handoff_skipped += 1
            continue

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
        handoff_imported += 1

    # 2. Sync detailed Antigravity conversation transcript turns
    convo_dir, convo_id = get_latest_convo_dir()
    turns_imported = 0
    turns_skipped = 0

    if convo_dir and convo_id:
        transcript_path = os.path.join(convo_dir, ".system_generated", "logs", "transcript.jsonl")
        turns = parse_transcript_turns(transcript_path)
        print(f"Detected active Antigravity session {convo_id} containing {len(turns)} turns.")
        
        for turn in turns:
            turn_text = f"User: {turn['user']}\nAssistant: {turn['assistant']}"
            content_hash = hashlib.sha256(turn_text.encode("utf-8")).hexdigest()
            mem_id = str(uuid.uuid5(uuid.NAMESPACE_DNS, content_hash))

            cursor = conn.execute("SELECT id FROM memories WHERE id = ?", (mem_id,))
            if cursor.fetchone():
                turns_skipped += 1
                continue

            memory_content = (
                f"Conversation Turn Context (Session: {convo_id}):\n"
                f"User asked: {turn['user']}\n"
                f"Agent reasoned/responded: {turn['assistant']}"
            )

            turn_time = turn["timestamp"] if turn["timestamp"] else now_iso

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
                    ?, 0.7,
                    NULL, NULL, NULL, '[]',
                    0, NULL,
                    '[]', NULL, '{}'
                )
                """,
                (
                    mem_id, memory_content, user_id, convo_id,
                    turn_time, turn_time, "conversation_turn"
                )
            )
            turns_imported += 1

    conn.commit()
    conn.close()

    print(f"Handoff Sync: Imported {handoff_imported} new, skipped {handoff_skipped} existing.")
    if convo_id:
        print(f"Antigravity Turns Sync: Imported {turns_imported} new, skipped {turns_skipped} existing.")

if __name__ == "__main__":
    main()
