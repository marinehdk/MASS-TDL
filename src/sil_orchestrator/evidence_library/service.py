from __future__ import annotations

import glob
import sqlite3
from contextlib import closing
from pathlib import Path
from typing import Any

from .config import EvidenceLibraryConfig, EvidenceRootConfig, load_effective_config
from .ingest import ingest_session, query_decision_frame, query_replay
from .store import initialize_schema, open_database


def open_initialized(config: EvidenceLibraryConfig | None = None) -> sqlite3.Connection:
    cfg = config or load_effective_config()
    conn = open_database(cfg)
    initialize_schema(conn)
    return conn


def _root_rows(config: EvidenceLibraryConfig) -> list[dict[str, Any]]:
    return [
        {
            "root_id": root.root_id,
            "label": root.label,
            "source": root.source,
            "path_glob": root.path_glob,
            "enabled": root.enabled,
            "trusted": root.trusted,
            "allow_retention_mutation": root.allow_retention_mutation,
            "follow_symlinks": root.follow_symlinks,
        }
        for root in config.roots
    ]


def _session_dirs(root: EvidenceRootConfig) -> list[Path]:
    session_dirs: list[Path] = []
    for match in sorted(glob.glob(root.path_glob)):
        root_path = Path(match)
        if not root_path.exists():
            continue
        if root_path.is_file():
            root_path = root_path.parent
        for manifest_path in sorted(root_path.glob("*/manifest.json")):
            session_dirs.append(manifest_path.parent)
    return session_dirs


def rescan_all(repo_root: Path | None = None, force: bool = False) -> dict[str, Any]:
    config = load_effective_config(repo_root=repo_root)
    ingested = 0
    errors: list[dict[str, str]] = []
    with closing(open_initialized(config)) as conn:
        for root in config.roots:
            if not root.enabled:
                continue
            for session_dir in _session_dirs(root):
                try:
                    ingest_session(
                        conn,
                        root,
                        session_dir,
                        raw_trace_policy=config.effective_retention_policy,
                        force=force,
                    )
                    ingested += 1
                except Exception as exc:  # pragma: no cover - defensive aggregation
                    errors.append({"path": str(session_dir), "error": str(exc)})
    return {"ingested": ingested, "errors": errors}


def ingest_frontend_session(session_dir: Path) -> str | None:
    config = load_effective_config()
    root = EvidenceRootConfig(
        root_id="frontend",
        label="Frontend evidence sessions",
        source="frontend",
        path_glob=str(session_dir.parent),
        enabled=True,
        trusted=True,
        allow_retention_mutation=False,
        follow_symlinks=False,
    )
    with closing(open_initialized(config)) as conn:
        result = ingest_session(
            conn,
            root,
            session_dir,
            raw_trace_policy=config.effective_retention_policy,
            force=True,
        )
    return result.evidence_id


def list_sessions(limit: int = 200) -> list[dict[str, Any]]:
    with closing(open_initialized()) as conn:
        rows = conn.execute(
            "select * from sessions order by coalesce(created_at, ended_at, session_id) desc limit ?",
            (max(1, min(limit, 500)),),
        ).fetchall()
        return [dict(row) for row in rows]


def get_replay(evidence_id: str, scenario_id: str) -> dict[str, Any]:
    with closing(open_initialized()) as conn:
        return query_replay(conn, evidence_id, scenario_id)


def get_decision_frame(evidence_id: str, scenario_id: str, sim_t: float) -> dict[str, Any]:
    with closing(open_initialized()) as conn:
        return query_decision_frame(conn, evidence_id, scenario_id, sim_t)


def get_config_payload(repo_root: Path | None = None) -> dict[str, Any]:
    config = load_effective_config(repo_root=repo_root)
    return {
        "config_home": str(config.config_home),
        "database_path": str(config.database_path),
        "raw_trace_policy": config.raw_trace_policy,
        "effective_retention_policy": config.effective_retention_policy,
        "roots": _root_rows(config),
    }
