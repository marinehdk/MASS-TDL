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
        if root_path.is_symlink() and not root.follow_symlinks:
            continue
        if root_path.is_file():
            root_path = root_path.parent
        resolved_root = root_path.resolve()
        for manifest_path in sorted(root_path.glob("*/manifest.json")):
            session_dir = manifest_path.parent
            if not root.follow_symlinks and (session_dir.is_symlink() or manifest_path.is_symlink()):
                continue
            try:
                session_dir.resolve().relative_to(resolved_root)
            except ValueError:
                continue
            session_dirs.append(session_dir)
    return session_dirs


def _root_for_session_dir(session_dir: Path, config: EvidenceLibraryConfig) -> EvidenceRootConfig | None:
    resolved_session = Path(session_dir).resolve()
    for root in config.roots:
        for match in sorted(glob.glob(root.path_glob)):
            root_path = Path(match)
            if not root_path.exists():
                continue
            if root_path.is_symlink() and not root.follow_symlinks:
                continue
            if root_path.is_file():
                root_path = root_path.parent
            if not root.follow_symlinks and Path(session_dir).is_symlink():
                continue
            try:
                resolved_session.relative_to(root_path.resolve())
            except ValueError:
                continue
            return root
    return None


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


def ingest_frontend_session(session_dir: Path, repo_root: Path | None = None) -> str | None:
    resolved_session = Path(session_dir).resolve()
    if repo_root is None:
        try:
            repo_root = resolved_session.parents[2]
        except IndexError:
            repo_root = None
    config = load_effective_config(repo_root=repo_root)
    root = _root_for_session_dir(session_dir, config) or EvidenceRootConfig(
        root_id="frontend",
        label="Frontend evidence sessions",
        source="frontend",
        path_glob=str(resolved_session.parent),
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


def _require_session_target(
    conn: sqlite3.Connection,
    *,
    evidence_id: str,
    scenario_id: str,
) -> None:
    if conn.execute("select 1 from sessions where evidence_id = ?", (evidence_id,)).fetchone() is None:
        raise LookupError(f"Evidence session not found: {evidence_id}")
    if (
        conn.execute(
            "select 1 from scenarios where evidence_id = ? and scenario_id = ?",
            (evidence_id, scenario_id),
        ).fetchone()
        is None
    ):
        raise LookupError(f"Scenario not found: {scenario_id}")


def list_sessions(limit: int = 200, repo_root: Path | None = None) -> list[dict[str, Any]]:
    with closing(open_initialized(load_effective_config(repo_root=repo_root))) as conn:
        rows = conn.execute(
            "select * from sessions order by coalesce(created_at, ended_at, session_id) desc limit ?",
            (max(1, min(limit, 500)),),
        ).fetchall()
        sessions = []
        for row in rows:
            session = dict(row)
            scenario_rows = conn.execute(
                "select scenario_id from scenarios where evidence_id = ? order by scenario_id",
                (session["evidence_id"],),
            ).fetchall()
            session["scenario_ids"] = [scenario["scenario_id"] for scenario in scenario_rows]
            sessions.append(session)
        return sessions


def get_replay(evidence_id: str, scenario_id: str, repo_root: Path | None = None) -> dict[str, Any]:
    with closing(open_initialized(load_effective_config(repo_root=repo_root))) as conn:
        _require_session_target(conn, evidence_id=evidence_id, scenario_id=scenario_id)
        return query_replay(conn, evidence_id, scenario_id)


def get_decision_frame(
    evidence_id: str,
    scenario_id: str,
    sim_t: float,
    repo_root: Path | None = None,
) -> dict[str, Any]:
    with closing(open_initialized(load_effective_config(repo_root=repo_root))) as conn:
        _require_session_target(conn, evidence_id=evidence_id, scenario_id=scenario_id)
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
