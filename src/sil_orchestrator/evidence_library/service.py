from __future__ import annotations

import glob
import json
import shutil
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
        try:
            _reject_symlink_components(root_path, "Configured evidence root match")
        except PermissionError:
            continue
        if not root_path.exists():
            continue
        if root_path.is_file():
            root_path = root_path.parent
        try:
            _reject_symlink_components(root_path, "Configured evidence root match")
            resolved_root = root_path.resolve(strict=True)
        except (OSError, PermissionError):
            continue
        manifest_path = root_path / "manifest.json"
        if manifest_path.exists() or manifest_path.is_symlink():
            if _session_path_is_healthy(root_path):
                session_dirs.append(root_path)
            continue
        for manifest_path in sorted(root_path.glob("*/manifest.json")):
            session_dir = manifest_path.parent
            if not _session_path_is_healthy(session_dir):
                continue
            try:
                session_dir.resolve(strict=True).relative_to(resolved_root)
            except (OSError, ValueError):
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
            if (root_path / "manifest.json").exists() and resolved_session != root_path.resolve():
                continue
            try:
                resolved_session.relative_to(root_path.resolve())
            except ValueError:
                continue
            return root
    return None


def _delete_evidence_rows(conn: sqlite3.Connection, evidence_id: str) -> None:
    for table in (
        "trajectory_downsample",
        "trajectory_samples",
        "state_segments",
        "events",
        "gate_results",
        "artifacts",
        "scenarios",
        "sessions",
    ):
        conn.execute("delete from " + table + " where evidence_id = ?", (evidence_id,))


def _absolute_safe_path(path: Path, label: str) -> Path:
    candidate = Path(path)
    if not candidate.is_absolute() or ".." in candidate.parts:
        raise PermissionError(f"{label} must be an absolute path without parent traversal")
    return candidate


def _reject_symlink_components(path: Path, label: str) -> None:
    candidate = _absolute_safe_path(path, label)
    current = Path(candidate.anchor)
    if current.is_symlink():
        raise PermissionError(f"{label} contains a symlink component")
    for part in candidate.parts[1:]:
        current /= part
        if current.is_symlink():
            raise PermissionError(f"{label} contains a symlink component")


def _session_path_is_healthy(session_path: Path) -> bool:
    try:
        session_path = _absolute_safe_path(session_path, "Evidence session path")
        manifest_path = session_path / "manifest.json"
        _reject_symlink_components(session_path, "Evidence session path")
        _reject_symlink_components(manifest_path, "Evidence session manifest path")
        return session_path.is_dir() and manifest_path.is_file()
    except (OSError, PermissionError):
        return False


def _literal_unified_run_id(session_path: Path) -> str | None:
    if session_path.name != "trace" or session_path.parent.parent.name != "runs":
        return None
    run_id = session_path.parent.name
    if run_id in {"", ".", ".."} or Path(run_id).parts != (run_id,):
        raise PermissionError("Unified evidence run ID must be one path component")
    return run_id


def _deletion_target(session_path: Path, session_id: str) -> Path:
    run_meta_path = session_path.parent / "run_meta.json"
    run_id = _literal_unified_run_id(session_path)
    if run_id is None:
        if session_path.name == "trace" and (run_meta_path.exists() or run_meta_path.is_symlink()):
            raise PermissionError("Unified evidence must use the literal runs/<run_id>/trace layout")
        return session_path
    if session_id != run_id:
        raise PermissionError("Unified run ID must match the indexed session ID")

    target = session_path.parent
    if not target.exists():
        return target

    _reject_symlink_components(run_meta_path, "Unified run metadata path")
    if not run_meta_path.is_file():
        raise PermissionError("Unified run metadata is missing")
    try:
        run_meta = json.loads(run_meta_path.read_text())
    except (json.JSONDecodeError, OSError, UnicodeDecodeError) as exc:
        raise PermissionError("Unified run metadata is invalid") from exc
    if not isinstance(run_meta, dict) or run_meta.get("run_id") != run_id:
        raise PermissionError("Unified run metadata does not match its run ID component")
    return target


def _matched_root_path(session_path: Path, root: EvidenceRootConfig) -> Path | None:
    session_path = _absolute_safe_path(session_path, "Evidence session path")
    root_glob = _absolute_safe_path(Path(root.path_glob), "Configured evidence root glob")
    _reject_symlink_components(session_path, "Evidence session path")
    resolved_session = session_path.resolve(strict=False)

    if session_path.match(str(root_glob)):
        return session_path

    for match in sorted(glob.glob(root.path_glob)):
        root_path = _absolute_safe_path(Path(match), "Configured evidence root match")
        if not root_path.exists():
            continue
        if root_path.is_file():
            root_path = root_path.parent
        resolved_root = root_path.resolve(strict=False)
        if (root_path / "manifest.json").exists() and resolved_session != resolved_root:
            continue
        try:
            resolved_session.relative_to(resolved_root)
        except ValueError:
            continue
        _reject_symlink_components(root_path, "Configured evidence root match")
        return root_path

    if not session_path.exists():
        for ancestor in session_path.parents:
            if ancestor.match(str(root_glob)):
                _reject_symlink_components(ancestor, "Configured evidence root match")
                return ancestor
    return None


def _resolve_deletion_target(
    *,
    session_id: str,
    session_path: Path,
    root_id: str,
    config: EvidenceLibraryConfig,
    repo_root: Path | None,
) -> tuple[Path, Path]:
    if repo_root is not None:
        _reject_symlink_components(Path(repo_root).absolute(), "Repository root")

    root = next((item for item in config.roots if item.root_id == root_id), None)
    if root is None:
        raise PermissionError("Evidence root is no longer configured")
    if not root.enabled or not root.trusted:
        raise PermissionError("Evidence root must be enabled and trusted for deletion")

    matched_root = _matched_root_path(session_path, root)
    if matched_root is None:
        raise PermissionError("Evidence session is outside its configured root match")

    target = _deletion_target(session_path, session_id)
    _reject_symlink_components(matched_root, "Configured evidence root match")
    _reject_symlink_components(target, "Evidence deletion target")
    resolved_session = session_path.resolve(strict=False)
    resolved_root = matched_root.resolve(strict=False)
    resolved_target = target.resolve(strict=False)
    if target != session_path:
        if resolved_root != resolved_session or resolved_target != resolved_session.parent:
            raise PermissionError("Unified run deletion requires the matched trace directory")
    else:
        try:
            relative_target = resolved_target.relative_to(resolved_root)
        except ValueError as exc:
            raise PermissionError("Legacy evidence target escaped configured root") from exc
        if relative_target == Path("."):
            raise PermissionError("Legacy evidence target must be below configured root")
    return target, resolved_target


def _prune_missing_sessions(conn: sqlite3.Connection) -> int:
    stale_ids = [
        row["evidence_id"]
        for row in conn.execute("select evidence_id, session_path from sessions")
        if not _session_path_is_healthy(Path(row["session_path"]))
    ]
    for evidence_id in stale_ids:
        _delete_evidence_rows(conn, evidence_id)
    conn.commit()
    return len(stale_ids)


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
        pruned = _prune_missing_sessions(conn)
    return {"ingested": ingested, "pruned": pruned, "errors": errors}


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
    config = load_effective_config(repo_root=repo_root)
    result_limit = max(1, min(limit, 500))
    with closing(open_initialized(config)) as conn:
        rows = conn.execute(
            "select * from sessions order by coalesce(created_at, ended_at, session_id) desc",
        ).fetchall()
        sessions = []
        for row in rows:
            session = dict(row)
            session_path = Path(session["session_path"])
            if not _session_path_is_healthy(session_path):
                continue
            try:
                _, resolved_target = _resolve_deletion_target(
                    session_id=str(session["session_id"]),
                    session_path=session_path,
                    root_id=str(session["root_id"]),
                    config=config,
                    repo_root=repo_root,
                )
            except (OSError, PermissionError) as exc:
                session["deletion_allowed"] = False
                session["deletion_target"] = None
                session["deletion_error"] = str(exc)
            else:
                session["deletion_allowed"] = True
                session["deletion_target"] = str(resolved_target)
                session["deletion_error"] = None
            scenario_rows = conn.execute(
                "select scenario_id, overall_pass from scenarios where evidence_id = ? order by scenario_id",
                (session["evidence_id"],),
            ).fetchall()
            session["scenario_ids"] = [scenario["scenario_id"] for scenario in scenario_rows]
            session["passed_scenarios"] = sum(1 for scenario in scenario_rows if scenario["overall_pass"] == 1)
            session["failed_scenarios"] = sum(1 for scenario in scenario_rows if scenario["overall_pass"] == 0)
            overview_rows = conn.execute(
                """
                select scenario_id, relative_path
                from artifacts
                where evidence_id = ?
                  and kind = 'trajectory_dashboard_png'
                  and available = 1
                order by scenario_id
                """,
                (session["evidence_id"],),
            ).fetchall()
            session["overview_pngs"] = [dict(overview) for overview in overview_rows]
            session["overview_png"] = session["overview_pngs"][0] if session["overview_pngs"] else None
            sessions.append(session)
            if len(sessions) >= result_limit:
                break
        return sessions


def get_overview_png_path(evidence_id: str, scenario_id: str | None = None, repo_root: Path | None = None) -> Path:
    config = load_effective_config(repo_root=repo_root)
    with closing(open_initialized(config)) as conn:
        args: tuple[Any, ...]
        scenario_clause = ""
        if scenario_id:
            scenario_clause = "and scenario_id = ?"
            args = (evidence_id, scenario_id)
        else:
            args = (evidence_id,)
        row = conn.execute(
            f"""
            select path
            from artifacts
            where evidence_id = ?
              and kind = 'trajectory_dashboard_png'
              and available = 1
              {scenario_clause}
            order by scenario_id
            limit 1
            """,
            args,
        ).fetchone()
        if row is None:
            raise LookupError(f"Overview PNG not found for evidence session: {evidence_id}")
        session = conn.execute(
            "select session_path, root_id from sessions where evidence_id = ?", (evidence_id,)
        ).fetchone()
        if session is None:
            raise LookupError(f"Evidence session not found: {evidence_id}")
        session_path = Path(session["session_path"])
        if not _session_path_is_healthy(session_path):
            raise LookupError(f"Evidence session path is not safe: {evidence_id}")
        root = next((item for item in config.roots if item.root_id == session["root_id"]), None)
        if root is None:
            raise LookupError(f"Evidence root is no longer configured: {session['root_id']}")
        try:
            matched_root = _matched_root_path(session_path, root)
            if matched_root is None:
                raise LookupError("Evidence session escaped its configured root")
            path = _absolute_safe_path(Path(row["path"]), "Overview PNG path")
            _reject_symlink_components(path, "Overview PNG path")
            resolved_path = path.resolve(strict=True)
            resolved_session = session_path.resolve(strict=True)
            resolved_path.relative_to(resolved_session)
        except (OSError, PermissionError, ValueError) as exc:
            raise LookupError("Overview PNG path escaped evidence session") from exc
        if not resolved_path.is_file():
            raise LookupError(f"Overview PNG not available for evidence session: {evidence_id}")
        return resolved_path


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


def delete_evidence_session(evidence_id: str, repo_root: Path | None = None) -> dict[str, Any]:
    config = load_effective_config(repo_root=repo_root)
    with closing(open_initialized(config)) as conn:
        row = conn.execute(
            "select session_id, session_path, root_id from sessions where evidence_id = ?", (evidence_id,)
        ).fetchone()
        if row is None:
            raise LookupError(f"Evidence session not found: {evidence_id}")

        target, resolved_target = _resolve_deletion_target(
            session_id=str(row["session_id"]),
            session_path=Path(row["session_path"]),
            root_id=str(row["root_id"]),
            config=config,
            repo_root=repo_root,
        )

        if not target.exists():
            _delete_evidence_rows(conn, evidence_id)
            conn.commit()
            return {
                "evidence_id": evidence_id,
                "deleted_path": str(resolved_target),
                "filesystem_deleted": False,
            }

        shutil.rmtree(target)
        _delete_evidence_rows(conn, evidence_id)
        conn.commit()
    return {
        "evidence_id": evidence_id,
        "deleted_path": str(resolved_target),
        "filesystem_deleted": True,
    }
