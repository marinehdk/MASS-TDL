from __future__ import annotations

import fcntl
import glob
import hashlib
import json
import os
import re
import shutil
import sqlite3
import stat
import threading
import uuid
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing, contextmanager
from dataclasses import dataclass
from datetime import datetime, timezone
from math import ceil
from pathlib import Path
from typing import Any, Callable, Iterator

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


_DELETION_STAGING_DIR = ".evidence-library-delete-staging"
_DELETION_RECOVERY_DIR = ".evidence-library-delete-recovery"
_DELETION_LOCK_DIR = ".evidence-library-delete-locks"
_DELETION_STAGE_METADATA_VERSION = 1


def _remove_empty_staging_dir(staging_dir: Path) -> None:
    try:
        staging_dir.rmdir()
    except OSError:
        pass


def _temporary_metadata_path(metadata_path: Path) -> Path:
    return metadata_path.with_name(metadata_path.name + ".tmp")


def _path_exists(path: Path) -> bool:
    return path.exists() or path.is_symlink()


def _recovery_dir_path(config: EvidenceLibraryConfig) -> Path:
    return Path(config.config_home).absolute() / _DELETION_RECOVERY_DIR


def _ensure_recovery_dir(config: EvidenceLibraryConfig) -> Path:
    recovery_dir = _recovery_dir_path(config)
    if _path_exists(recovery_dir):
        _reject_symlink_components(recovery_dir, "Evidence deletion recovery directory")
    else:
        recovery_dir.mkdir(mode=0o700, parents=True)
    _reject_symlink_components(recovery_dir, "Evidence deletion recovery directory")
    if not recovery_dir.is_dir():
        raise OSError("Evidence deletion recovery path is not a directory")
    return recovery_dir


def _recovery_record_path(config: EvidenceLibraryConfig, staged_target: Path) -> Path:
    return _recovery_dir_path(config) / f"{staged_target.name}.json"


def _remove_empty_recovery_dir(config: EvidenceLibraryConfig) -> None:
    try:
        _recovery_dir_path(config).rmdir()
    except OSError:
        pass


class _DeletionLockConflict(PermissionError):
    pass


def _deletion_lock_dir_path(config: EvidenceLibraryConfig) -> Path:
    return Path(config.config_home).absolute() / _DELETION_LOCK_DIR


@contextmanager
def _deletion_file_lock(
    config: EvidenceLibraryConfig,
    lock_name: str,
    mode: int,
    conflict_message: str,
) -> Iterator[None]:
    lock_dir = _deletion_lock_dir_path(config)
    lock_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
    _reject_symlink_components(lock_dir, "Evidence deletion lock directory")
    if not lock_dir.is_dir():
        raise OSError("Evidence deletion lock path is not a directory")

    lock_path = lock_dir / lock_name
    if _path_exists(lock_path):
        _reject_symlink_components(lock_path, "Evidence deletion lock")
    flags = os.O_CREAT | os.O_RDWR
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    descriptor = os.open(lock_path, flags, 0o600)
    acquired = False
    try:
        try:
            fcntl.flock(descriptor, mode | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise _DeletionLockConflict(conflict_message) from exc
        acquired = True
        yield
    finally:
        if acquired:
            fcntl.flock(descriptor, fcntl.LOCK_UN)
        os.close(descriptor)


@contextmanager
def _evidence_deletion_lock(
    config: EvidenceLibraryConfig,
    evidence_id: str,
) -> Iterator[None]:
    digest = hashlib.sha256(evidence_id.encode()).hexdigest()
    with _deletion_file_lock(
        config,
        f"{digest}.lock",
        fcntl.LOCK_EX,
        "Evidence deletion is already in progress",
    ):
        yield


@contextmanager
def _deletion_scan_coordination_lock(
    config: EvidenceLibraryConfig,
    *,
    exclusive: bool,
) -> Iterator[None]:
    with _deletion_file_lock(
        config,
        "scan-coordination.lock",
        fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH,
        "evidence deletion is in progress" if exclusive else "Evidence library rescan is in progress",
    ):
        yield


def _deletion_stage_paths(evidence_id: str, target: Path) -> tuple[Path, Path, Path]:
    staging_dir = target.parent / _DELETION_STAGING_DIR
    digest = hashlib.sha256(f"{evidence_id}\0{target}".encode()).hexdigest()[:24]
    staged_target = staging_dir / f"delete-{digest}"
    metadata_path = staging_dir / f"{staged_target.name}.json"
    return staging_dir, staged_target, metadata_path


def _stage_metadata_payload(evidence_id: str, target: Path, staged_target: Path) -> dict[str, Any]:
    return {
        "evidence_id": evidence_id,
        "original_path": str(target),
        "staged_path": str(staged_target),
        "version": _DELETION_STAGE_METADATA_VERSION,
    }


def _write_stage_metadata(metadata_path: Path, payload: dict[str, Any]) -> None:
    temporary_path = _temporary_metadata_path(metadata_path)
    if metadata_path.exists() or metadata_path.is_symlink() or temporary_path.exists():
        raise OSError("Evidence deletion staging metadata already exists")
    try:
        with temporary_path.open("x", encoding="utf-8") as stream:
            json.dump(payload, stream, sort_keys=True)
            stream.write("\n")
        temporary_path.chmod(0o600)
        temporary_path.replace(metadata_path)
    except Exception:
        temporary_path.unlink(missing_ok=True)
        raise


def _read_metadata_payload(metadata_path: Path) -> dict[str, Any]:
    _reject_symlink_components(metadata_path, "Evidence deletion staging metadata")
    if not metadata_path.is_file():
        raise OSError("Evidence deletion staging metadata is missing")
    try:
        payload = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise OSError("Evidence deletion staging metadata is invalid") from exc
    if not isinstance(payload, dict):
        raise OSError("Evidence deletion staging metadata is invalid")
    return payload


def _read_stage_metadata(
    metadata_path: Path,
    *,
    evidence_id: str,
    target: Path,
    staged_target: Path,
) -> dict[str, Any]:
    payload = _read_metadata_payload(metadata_path)
    if payload != _stage_metadata_payload(evidence_id, target, staged_target):
        raise OSError("Evidence deletion staging metadata does not match its target")
    return payload


def _stage_deletion_target(
    evidence_id: str,
    target: Path,
    config: EvidenceLibraryConfig,
) -> tuple[Path, Path, Path, Path]:
    staging_dir, staged_target, metadata_path = _deletion_stage_paths(evidence_id, target)
    if staging_dir.exists() or staging_dir.is_symlink():
        _reject_symlink_components(staging_dir, "Evidence deletion staging directory")
    else:
        staging_dir.mkdir(mode=0o700)
    _reject_symlink_components(staging_dir, "Evidence deletion staging directory")
    if not staging_dir.is_dir():
        raise OSError("Evidence deletion staging path is not a directory")

    if staged_target.exists() or staged_target.is_symlink():
        raise OSError("Evidence deletion staging target already exists")
    recovery_record_path = _recovery_record_path(config, staged_target)
    payload = _stage_metadata_payload(evidence_id, target, staged_target)
    _ensure_recovery_dir(config)
    _write_stage_metadata(metadata_path, payload)
    try:
        _write_stage_metadata(recovery_record_path, payload)
    except Exception:
        metadata_path.unlink(missing_ok=True)
        _remove_empty_staging_dir(staging_dir)
        _remove_empty_recovery_dir(config)
        raise
    try:
        target.rename(staged_target)
    except OSError:
        metadata_path.unlink(missing_ok=True)
        recovery_record_path.unlink(missing_ok=True)
        _temporary_metadata_path(recovery_record_path).unlink(missing_ok=True)
        _remove_empty_staging_dir(staging_dir)
        _remove_empty_recovery_dir(config)
        raise
    return staging_dir, staged_target, metadata_path, recovery_record_path


def _restore_staged_target(
    staging_dir: Path,
    staged_target: Path,
    metadata_path: Path,
    recovery_record_path: Path,
    *,
    evidence_id: str,
    target: Path,
) -> None:
    _read_stage_metadata(
        metadata_path,
        evidence_id=evidence_id,
        target=target,
        staged_target=staged_target,
    )
    _reject_symlink_components(staged_target, "Evidence deletion staged target")
    if not staged_target.is_dir():
        raise OSError("Evidence deletion staged target is not a directory")
    if target.exists() or target.is_symlink():
        raise OSError("Evidence deletion target was recreated before restoration")
    staged_target.rename(target)
    metadata_path.unlink(missing_ok=True)
    _temporary_metadata_path(metadata_path).unlink(missing_ok=True)
    recovery_record_path.unlink(missing_ok=True)
    _temporary_metadata_path(recovery_record_path).unlink(missing_ok=True)
    _remove_empty_staging_dir(staging_dir)
    try:
        recovery_record_path.parent.rmdir()
    except OSError:
        pass


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


def _indexed_deletion_target(session_id: str, session_path: Path) -> Path:
    session_path = _absolute_safe_path(session_path, "Indexed evidence session path")
    run_id = _literal_unified_run_id(session_path)
    if run_id is None:
        return session_path
    if session_id != run_id:
        raise PermissionError("Indexed unified run ID does not match its path")
    return session_path.parent


def _stage_metadata_sources(
    config: EvidenceLibraryConfig,
    staged_target: Path,
    metadata_path: Path,
) -> tuple[Path, Path, Path, Path]:
    recovery_record_path = _recovery_record_path(config, staged_target)
    return (
        metadata_path,
        _temporary_metadata_path(metadata_path),
        recovery_record_path,
        _temporary_metadata_path(recovery_record_path),
    )


def _clear_stage_metadata_sources(paths: tuple[Path, ...]) -> None:
    for path in paths:
        if not _path_exists(path):
            continue
        _reject_symlink_components(path, "Evidence deletion recovery metadata")
        path.unlink()


def _has_indexed_recovery_artifacts(
    config: EvidenceLibraryConfig,
    evidence_id: str,
    target: Path,
) -> bool:
    _, staged_target, metadata_path = _deletion_stage_paths(evidence_id, target)
    return _path_exists(staged_target) or any(
        _path_exists(path)
        for path in _stage_metadata_sources(config, staged_target, metadata_path)
    )


def _recover_indexed_deletion(
    config: EvidenceLibraryConfig,
    evidence_id: str,
    target: Path,
) -> bool:
    staging_dir, staged_target, metadata_path = _deletion_stage_paths(evidence_id, target)
    metadata_sources = _stage_metadata_sources(config, staged_target, metadata_path)
    target_exists = _path_exists(target)
    staged_target_exists = _path_exists(staged_target)
    has_metadata = any(_path_exists(path) for path in metadata_sources)
    if not staged_target_exists and not has_metadata:
        return False

    if target_exists and not staged_target_exists:
        _clear_stage_metadata_sources(metadata_sources)
        _remove_empty_staging_dir(staging_dir)
        _remove_empty_recovery_dir(config)
        return True
    if target_exists or not staged_target_exists:
        raise OSError("Evidence deletion recovery state is ambiguous")
    _reject_symlink_components(staged_target, "Evidence deletion staged target")
    if not staged_target.is_dir():
        raise OSError("Evidence deletion staged target is not a directory")

    valid_metadata_sources: list[Path] = []
    for source in metadata_sources:
        if not _path_exists(source):
            continue
        try:
            _read_stage_metadata(
                source,
                evidence_id=evidence_id,
                target=target,
                staged_target=staged_target,
            )
        except (OSError, PermissionError):
            continue
        valid_metadata_sources.append(source)
    if not valid_metadata_sources:
        raise OSError("Evidence deletion recovery metadata is unavailable")

    recovery_record_path = _recovery_record_path(config, staged_target)
    if metadata_path not in valid_metadata_sources:
        payload = _stage_metadata_payload(evidence_id, target, staged_target)
        metadata_tmp_path = _temporary_metadata_path(metadata_path)
        if metadata_tmp_path in valid_metadata_sources:
            if _path_exists(metadata_path):
                _clear_stage_metadata_sources((metadata_path,))
            metadata_tmp_path.replace(metadata_path)
        else:
            _clear_stage_metadata_sources((metadata_path, metadata_tmp_path))
            _write_stage_metadata(metadata_path, payload)
    _restore_staged_target(
        staging_dir,
        staged_target,
        metadata_path,
        recovery_record_path,
        evidence_id=evidence_id,
        target=target,
    )
    return True


def _validated_recovery_record(
    config: EvidenceLibraryConfig,
    record_path: Path,
) -> tuple[str, Path, Path, Path]:
    payload = _read_metadata_payload(record_path)
    if set(payload) != {"evidence_id", "original_path", "staged_path", "version"}:
        raise OSError("Evidence deletion recovery record is invalid")
    evidence_id = payload.get("evidence_id")
    original_path = payload.get("original_path")
    staged_path = payload.get("staged_path")
    if (
        not isinstance(evidence_id, str)
        or not evidence_id
        or not isinstance(original_path, str)
        or not isinstance(staged_path, str)
        or payload.get("version") != _DELETION_STAGE_METADATA_VERSION
    ):
        raise OSError("Evidence deletion recovery record is invalid")

    target = _absolute_safe_path(Path(original_path), "Evidence deletion recovery target")
    staged_target = _absolute_safe_path(Path(staged_path), "Evidence deletion recovery stage")
    _, expected_staged_target, metadata_path = _deletion_stage_paths(evidence_id, target)
    expected_record_path = _recovery_record_path(config, expected_staged_target)
    if staged_target != expected_staged_target or record_path != expected_record_path:
        raise OSError("Evidence deletion recovery record is not deterministic")
    return evidence_id, target, staged_target, metadata_path


def _validate_postcommit_recovery_paths(
    config: EvidenceLibraryConfig,
    target: Path,
    staged_target: Path,
    metadata_path: Path,
) -> tuple[Path, int, Path, Path, Path]:
    _reject_symlink_components(target, "Evidence deletion recovery target")
    _reject_symlink_components(staged_target, "Evidence deletion recovery stage")
    _reject_symlink_components(metadata_path, "Evidence deletion recovery metadata")

    unified_session = target / "trace"
    unified_run_id = _literal_unified_run_id(unified_session)
    for root in config.roots:
        if not root.enabled or not root.trusted:
            continue

        try:
            matched_root = _matched_root_path(target, root)
        except (OSError, PermissionError):
            matched_root = None
        if matched_root is not None:
            try:
                relative_target = target.relative_to(matched_root)
            except ValueError:
                pass
            else:
                if relative_target != Path("."):
                    relative_staged_target = staged_target.relative_to(matched_root)
                    relative_metadata_path = metadata_path.relative_to(matched_root)
                    anchor_path = matched_root
                    anchor_descriptor = _open_directory_without_symlinks(anchor_path)
                    return (
                        anchor_path,
                        anchor_descriptor,
                        relative_target,
                        relative_staged_target,
                        relative_metadata_path,
                    )

        if unified_run_id is None or unified_run_id != target.name:
            continue
        try:
            matched_trace_root = _matched_root_path(unified_session, root)
        except (OSError, PermissionError):
            continue
        if (
            matched_trace_root is not None
            and matched_trace_root == unified_session
        ):
            anchor_path = target.parent
            relative_staged_target = staged_target.relative_to(anchor_path)
            relative_metadata_path = metadata_path.relative_to(anchor_path)
            anchor_descriptor = _open_directory_without_symlinks(anchor_path)
            return (
                anchor_path,
                anchor_descriptor,
                Path(target.name),
                relative_staged_target,
                relative_metadata_path,
            )

    raise PermissionError("Evidence deletion recovery target is outside configured trusted roots")


def _directory_open_flags() -> int:
    flags = os.O_RDONLY | os.O_DIRECTORY
    if hasattr(os, "O_CLOEXEC"):
        flags |= os.O_CLOEXEC
    if not hasattr(os, "O_NOFOLLOW"):
        raise OSError("Symlink-safe evidence deletion recovery is unsupported")
    flags |= os.O_NOFOLLOW
    return flags


def _open_directory_without_symlinks(path: Path) -> int:
    candidate = _absolute_safe_path(path, "Evidence deletion recovery anchor")
    descriptor = os.open(candidate.anchor, _directory_open_flags())
    try:
        for part in candidate.parts[1:]:
            next_descriptor = os.open(
                part,
                _directory_open_flags(),
                dir_fd=descriptor,
            )
            os.close(descriptor)
            descriptor = next_descriptor
        return descriptor
    except Exception:
        os.close(descriptor)
        raise


def _relative_path_state(
    anchor_descriptor: int,
    relative_path: Path,
) -> tuple[str, tuple[tuple[int, int, int], ...]]:
    if relative_path.is_absolute() or not relative_path.parts or ".." in relative_path.parts:
        raise PermissionError("Evidence deletion recovery path escaped its trusted anchor")

    descriptor = os.dup(anchor_descriptor)
    identities: list[tuple[int, int, int]] = []
    try:
        anchor_stat = os.fstat(descriptor)
        identities.append((anchor_stat.st_dev, anchor_stat.st_ino, stat.S_IFMT(anchor_stat.st_mode)))
        for part in relative_path.parts[:-1]:
            try:
                next_descriptor = os.open(
                    part,
                    _directory_open_flags(),
                    dir_fd=descriptor,
                )
            except FileNotFoundError:
                return "missing", tuple(identities)
            os.close(descriptor)
            descriptor = next_descriptor
            component_stat = os.fstat(descriptor)
            identities.append((
                component_stat.st_dev,
                component_stat.st_ino,
                stat.S_IFMT(component_stat.st_mode),
            ))

        try:
            final_stat = os.stat(
                relative_path.name,
                dir_fd=descriptor,
                follow_symlinks=False,
            )
        except FileNotFoundError:
            return "missing", tuple(identities)
        if stat.S_ISLNK(final_stat.st_mode):
            raise PermissionError("Evidence deletion recovery path is a symlink")
        identities.append((
            final_stat.st_dev,
            final_stat.st_ino,
            stat.S_IFMT(final_stat.st_mode),
        ))
        if stat.S_ISDIR(final_stat.st_mode):
            kind = "directory"
        elif stat.S_ISREG(final_stat.st_mode):
            kind = "file"
        else:
            kind = "other"
        return kind, tuple(identities)
    finally:
        os.close(descriptor)


def _revalidate_recovery_path_state(
    anchor_path: Path,
    anchor_descriptor: int,
    relative_path: Path,
    expected_state: tuple[str, tuple[tuple[int, int, int], ...]],
) -> None:
    current_descriptor = _open_directory_without_symlinks(anchor_path)
    try:
        current_anchor = os.fstat(current_descriptor)
        trusted_anchor = os.fstat(anchor_descriptor)
        if (current_anchor.st_dev, current_anchor.st_ino) != (
            trusted_anchor.st_dev,
            trusted_anchor.st_ino,
        ):
            raise PermissionError("Evidence deletion recovery anchor changed")
    finally:
        os.close(current_descriptor)
    if _relative_path_state(anchor_descriptor, relative_path) != expected_state:
        raise PermissionError("Evidence deletion recovery path changed")


def _valid_recovery_records_by_evidence(
    config: EvidenceLibraryConfig,
) -> dict[str, list[tuple[Path, Path, Path, Path]]]:
    recovery_dir = _recovery_dir_path(config)
    if not _path_exists(recovery_dir):
        return {}
    _reject_symlink_components(recovery_dir, "Evidence deletion recovery directory")
    if not recovery_dir.is_dir():
        raise OSError("Evidence deletion recovery path is not a directory")

    records: dict[str, list[tuple[Path, Path, Path, Path]]] = {}
    for record_path in sorted(recovery_dir.glob("*.json")):
        try:
            evidence_id, target, staged_target, metadata_path = _validated_recovery_record(
                config, record_path
            )
        except (OSError, PermissionError):
            continue
        records.setdefault(evidence_id, []).append(
            (record_path, target, staged_target, metadata_path)
        )
    return records


def _discover_postcommit_cleanup(
    conn: sqlite3.Connection,
    config: EvidenceLibraryConfig,
) -> tuple[list[dict[str, str]], list[dict[str, Any]]]:
    errors: list[dict[str, str]] = []
    pending: list[dict[str, Any]] = []
    recovery_dir = _recovery_dir_path(config)
    if not _path_exists(recovery_dir):
        return errors, pending
    try:
        _reject_symlink_components(recovery_dir, "Evidence deletion recovery directory")
        if not recovery_dir.is_dir():
            raise OSError("Evidence deletion recovery path is not a directory")
    except (OSError, PermissionError):
        return ([{
            "path": str(recovery_dir),
            "error": "interrupted deletion recovery failed",
        }], pending)

    for record_path in sorted(recovery_dir.glob("*.json")):
        try:
            evidence_id, target, staged_target, metadata_path = _validated_recovery_record(
                config, record_path
            )
            if conn.execute(
                "select 1 from sessions where evidence_id = ?", (evidence_id,)
            ).fetchone() is not None:
                continue

            (
                anchor_path,
                anchor_descriptor,
                relative_target,
                relative_staged_target,
                relative_metadata_path,
            ) = _validate_postcommit_recovery_paths(
                config,
                target,
                staged_target,
                metadata_path,
            )
            try:
                target_state = _relative_path_state(anchor_descriptor, relative_target)
                staged_target_state = _relative_path_state(
                    anchor_descriptor,
                    relative_staged_target,
                )
                metadata_state = _relative_path_state(
                    anchor_descriptor,
                    relative_metadata_path,
                )
                _revalidate_recovery_path_state(
                    anchor_path,
                    anchor_descriptor,
                    relative_target,
                    target_state,
                )
                _revalidate_recovery_path_state(
                    anchor_path,
                    anchor_descriptor,
                    relative_staged_target,
                    staged_target_state,
                )
                _revalidate_recovery_path_state(
                    anchor_path,
                    anchor_descriptor,
                    relative_metadata_path,
                    metadata_state,
                )
            finally:
                os.close(anchor_descriptor)

            target_kind = target_state[0]
            staged_target_kind = staged_target_state[0]
            metadata_kind = metadata_state[0]
            if (
                target_kind == "missing"
                and staged_target_kind in {"missing", "directory"}
                and metadata_kind in {"missing", "file"}
                and (staged_target_kind == "directory" or metadata_kind == "file")
            ):
                cleanup_paths: list[str] = []
                if staged_target_kind == "directory":
                    cleanup_paths.append(str(staged_target))
                if metadata_kind == "file":
                    cleanup_paths.append(str(metadata_path))
                cleanup_paths.append(str(record_path))
                pending.append({
                    "evidence_id": evidence_id,
                    "deleted_path": str(target),
                    "filesystem_deleted": False,
                    "filesystem_cleanup": "pending",
                    "cleanup_error": "staged filesystem cleanup is pending",
                    "cleanup_path": cleanup_paths[0],
                    "cleanup_metadata_path": str(record_path),
                    "cleanup_paths": cleanup_paths,
                })
                continue
            if not (
                staged_target_kind == "missing"
                and target_kind in {"missing", "directory"}
            ):
                raise OSError("Evidence deletion cleanup recovery state is ambiguous")

            # Root-local artifacts remain untouched when no staged payload remains.
            # The central record can be retired after anchored state verification.
            _reject_symlink_components(record_path, "Evidence deletion recovery metadata")
            record_path.unlink()
            _temporary_metadata_path(record_path).unlink(missing_ok=True)
            _remove_empty_recovery_dir(config)
        except (OSError, PermissionError):
            errors.append({
                "path": str(record_path),
                "error": "interrupted deletion recovery failed",
            })

    for temporary_record in sorted(recovery_dir.glob("*.json.tmp")):
        final_record = temporary_record.with_name(temporary_record.name[:-4])
        if _path_exists(final_record):
            try:
                _reject_symlink_components(temporary_record, "Evidence deletion recovery metadata")
                temporary_record.unlink()
            except (OSError, PermissionError):
                errors.append({
                    "path": str(temporary_record),
                    "error": "interrupted deletion recovery failed",
                })
            continue
        errors.append({
            "path": str(temporary_record),
            "error": "interrupted deletion recovery failed",
        })
    return errors, pending


def _recover_interrupted_deletions(
    conn: sqlite3.Connection,
    config: EvidenceLibraryConfig,
    repo_root: Path | None,
) -> tuple[list[dict[str, str]], set[str], list[dict[str, Any]]]:
    errors: list[dict[str, str]] = []
    protected_ids: set[str] = set()
    try:
        recovery_records = _valid_recovery_records_by_evidence(config)
    except (OSError, PermissionError):
        recovery_records = {}
        errors.append({
            "path": str(_recovery_dir_path(config)),
            "error": "interrupted deletion recovery failed",
        })

    rows = conn.execute(
        "select evidence_id, session_id, session_path, root_id from sessions"
    ).fetchall()
    for row in rows:
        evidence_id = str(row["evidence_id"])
        linked_records = recovery_records.get(evidence_id, [])
        linked_targets = {record[1] for record in linked_records}
        try:
            indexed_target = _indexed_deletion_target(
                str(row["session_id"]), Path(row["session_path"])
            )
        except (OSError, PermissionError):
            indexed_target = None
        target = next(iter(linked_targets)) if len(linked_targets) == 1 else indexed_target

        if target is None or len(linked_targets) > 1:
            if linked_records:
                protected_ids.add(evidence_id)
                errors.append({
                    "path": str(linked_records[0][2].parent),
                    "error": "interrupted deletion recovery failed",
                })
            continue

        try:
            resolved_target, _ = _resolve_deletion_target(
                session_id=str(row["session_id"]),
                session_path=Path(row["session_path"]),
                root_id=str(row["root_id"]),
                config=config,
                repo_root=repo_root,
            )
            if resolved_target != target:
                raise PermissionError("Indexed deletion target changed during recovery")
        except (OSError, PermissionError):
            if linked_records or _has_indexed_recovery_artifacts(config, evidence_id, target):
                protected_ids.add(evidence_id)
                errors.append({
                    "path": str(target.parent / _DELETION_STAGING_DIR),
                    "error": "interrupted deletion recovery failed",
                })
            continue

        try:
            _recover_indexed_deletion(config, evidence_id, target)
        except (OSError, PermissionError):
            protected_ids.add(evidence_id)
            errors.append({
                "path": str(target.parent / _DELETION_STAGING_DIR),
                "error": "interrupted deletion recovery failed",
            })

    cleanup_errors, cleanup_pending = _discover_postcommit_cleanup(conn, config)
    errors.extend(cleanup_errors)
    return errors, protected_ids, cleanup_pending


def _prune_missing_sessions(
    conn: sqlite3.Connection,
    protected_ids: set[str] | None = None,
) -> int:
    protected = protected_ids or set()
    stale_ids = [
        row["evidence_id"]
        for row in conn.execute("select evidence_id, session_path from sessions")
        if row["evidence_id"] not in protected
        and not _session_path_is_healthy(Path(row["session_path"]))
    ]
    for evidence_id in stale_ids:
        _delete_evidence_rows(conn, evidence_id)
    conn.commit()
    return len(stale_ids)


def _rescan_all_locked(
    config: EvidenceLibraryConfig,
    repo_root: Path | None,
    force: bool,
    progress_callback: Callable[..., None] | None = None,
) -> dict[str, Any]:
    ingested = 0
    skipped = 0
    processed = 0
    errors: list[dict[str, str]] = []
    discovered_sessions = [
        (root, session_dir)
        for root in config.roots
        if root.enabled
        for session_dir in _session_dirs(root)
    ]
    if progress_callback is not None:
        progress_callback(total=len(discovered_sessions))
    with closing(open_initialized(config)) as conn:
        recovery_errors, protected_ids, cleanup_pending = _recover_interrupted_deletions(
            conn, config, repo_root
        )
        errors.extend(recovery_errors)
        for root, session_dir in discovered_sessions:
            try:
                result = ingest_session(
                    conn,
                    root,
                    session_dir,
                    raw_trace_policy=config.effective_retention_policy,
                    force=force,
                )
                if result.skipped:
                    skipped += 1
                else:
                    ingested += 1
            except Exception as exc:  # pragma: no cover - defensive aggregation
                errors.append({"path": str(session_dir), "error": str(exc)})
            finally:
                processed += 1
                if progress_callback is not None:
                    progress_callback(
                        processed=processed,
                        ingested=ingested,
                        skipped=skipped,
                        errors=list(errors),
                    )
        pruned = _prune_missing_sessions(conn, protected_ids)
    return {
        "processed": processed,
        "ingested": ingested,
        "skipped": skipped,
        "pruned": pruned,
        "errors": errors,
        "cleanup_pending": cleanup_pending,
    }


def rescan_all(
    repo_root: Path | None = None,
    force: bool = False,
    progress_callback: Callable[..., None] | None = None,
) -> dict[str, Any]:
    config = load_effective_config(repo_root=repo_root)
    try:
        with _deletion_scan_coordination_lock(config, exclusive=True):
            return _rescan_all_locked(config, repo_root, force, progress_callback)
    except _DeletionLockConflict as exc:
        return {
            "processed": 0,
            "ingested": 0,
            "skipped": 0,
            "pruned": 0,
            "errors": [{
                "path": str(_deletion_lock_dir_path(config)),
                "error": str(exc),
            }],
            "cleanup_pending": [],
        }


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


class EvidenceRescanManager:
    _ACTIVE_STATES = {"queued", "running"}
    _PROGRESS_FIELDS = {
        "total",
        "processed",
        "ingested",
        "skipped",
        "pruned",
        "errors",
        "cleanup_pending",
    }

    def __init__(self, runner: Callable[..., dict[str, Any]] | None = None) -> None:
        self._runner = runner or rescan_all
        self._executor = ThreadPoolExecutor(
            max_workers=1,
            thread_name_prefix="evidence-library-rescan",
        )
        self._lock = threading.Lock()
        self._snapshot = self._new_snapshot()

    @staticmethod
    def _new_snapshot(*, job_id: str | None = None, force: bool = False) -> dict[str, Any]:
        return {
            "job_id": job_id,
            "state": "idle" if job_id is None else "queued",
            "force": force,
            "total": 0,
            "processed": 0,
            "ingested": 0,
            "skipped": 0,
            "pruned": 0,
            "errors": [],
            "cleanup_pending": [],
            "started_at": None,
            "finished_at": None,
        }

    @staticmethod
    def _copy_snapshot(snapshot: dict[str, Any]) -> dict[str, Any]:
        copied = dict(snapshot)
        copied["errors"] = [dict(item) for item in snapshot["errors"]]
        copied["cleanup_pending"] = [dict(item) for item in snapshot["cleanup_pending"]]
        return copied

    def start(self, *, repo_root: Path | None, force: bool) -> dict[str, Any]:
        with self._lock:
            if self._snapshot["state"] in self._ACTIVE_STATES:
                return self._copy_snapshot(self._snapshot)
            job_id = uuid.uuid4().hex
            self._snapshot = self._new_snapshot(job_id=job_id, force=force)
            queued_snapshot = self._copy_snapshot(self._snapshot)
            self._executor.submit(self._run, job_id, repo_root, force)
            return queued_snapshot

    def status(self) -> dict[str, Any]:
        with self._lock:
            return self._copy_snapshot(self._snapshot)

    def _update_progress(self, job_id: str, **updates: Any) -> None:
        with self._lock:
            if self._snapshot["job_id"] != job_id:
                return
            for key, value in updates.items():
                if key in self._PROGRESS_FIELDS:
                    self._snapshot[key] = list(value) if key in {"errors", "cleanup_pending"} else value

    def _run(self, job_id: str, repo_root: Path | None, force: bool) -> None:
        with self._lock:
            if self._snapshot["job_id"] != job_id:
                return
            self._snapshot["state"] = "running"
            self._snapshot["started_at"] = _utc_now()
        try:
            result = self._runner(
                repo_root=repo_root,
                force=force,
                progress_callback=lambda **updates: self._update_progress(job_id, **updates),
            )
        except Exception as exc:  # pragma: no cover - guarded by route tests
            with self._lock:
                if self._snapshot["job_id"] == job_id:
                    self._snapshot["state"] = "failed"
                    self._snapshot["errors"] = [{"path": "rescan", "error": str(exc)}]
                    self._snapshot["finished_at"] = _utc_now()
            return
        with self._lock:
            if self._snapshot["job_id"] != job_id:
                return
            for key in self._PROGRESS_FIELDS:
                if key in result:
                    value = result[key]
                    self._snapshot[key] = list(value) if key in {"errors", "cleanup_pending"} else value
            self._snapshot["state"] = "completed"
            self._snapshot["finished_at"] = _utc_now()


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


@dataclass(frozen=True)
class EvidenceSessionListQuery:
    page: int = 1
    page_size: int = 20
    search: str = ""
    sort_key: str = "time"
    sort_direction: str = "desc"
    result: str | None = None
    scenario_count: int | None = None
    mode: str | None = None
    scenario: str | None = None
    source: str | None = None
    worktree: str | None = None


_MODE_LABELS = {
    "debug": "调试验证",
    "cohort": "同类验证",
    "full": "完整验证",
    "avoidance": "避碰验证",
}
_OUTCOME_LABELS = {"passed": "通过", "failed": "不通过", "unknown": "-"}
_SOURCE_LABELS = {"cli": "CLI", "front": "Front"}
_MODE_SORT_ORDER = {"avoidance": 0, "debug": 1, "cohort": 2, "full": 3}
_OUTCOME_SORT_ORDER = {"unknown": 0, "failed": 1, "passed": 2}
_NATURAL_TOKEN_RE = re.compile(r"\d+|\D+")
_SESSION_ID_TIME_RE = re.compile(r"^(\d{8})_(\d{6})")


def _natural_text_sort_key(value: Any) -> tuple[tuple[int, Any], ...]:
    return tuple(
        (1, int(token)) if token.isdigit() else (0, token.casefold())
        for token in _NATURAL_TOKEN_RE.findall(str(value))
    )


def _absolute_time_sort_key(value: Any) -> tuple[int, Any]:
    text = str(value or "")
    try:
        parsed = datetime.fromisoformat(text[:-1] + "+00:00" if text.endswith("Z") else text)
    except ValueError:
        match = _SESSION_ID_TIME_RE.match(text)
        if match is None:
            return (0, _natural_text_sort_key(text))
        parsed = datetime.strptime("".join(match.groups()), "%Y%m%d%H%M%S")
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return (1, parsed.astimezone(timezone.utc).timestamp())


def _session_list_mode(session: dict[str, Any]) -> str:
    text = f"{session['session_id']} {session['suite']}".lower()
    if any(marker in text for marker in ("debug", "dbg", "trace", "ctx")):
        return "debug"
    if "cohort" in text:
        return "cohort"
    if any(marker in text for marker in ("full", "clean8", "clean12")):
        return "full"
    if "fast" in text or session["suite"] == "single":
        return "avoidance"
    return "debug"


def _session_list_source(source: Any) -> tuple[str, str]:
    raw_source = str(source or "")
    canonical = raw_source.lower()
    if canonical in {"frontend", "front"}:
        canonical = "front"
    if not canonical:
        canonical = "-"
    return canonical, _SOURCE_LABELS.get(canonical, raw_source or "-")


def _session_list_worktree(session: dict[str, Any], source_label: str) -> str:
    if source_label == "Front":
        return ""
    if session.get("worktree_name"):
        return str(session["worktree_name"])
    session_path = str(session["session_path"])
    marker = "/.worktrees/"
    if marker in session_path:
        return session_path.split(marker, 1)[1].split("/", 1)[0]
    return str(session.get("branch") or "-")


def _build_session_list_records(
    rows: list[sqlite3.Row],
    scenarios_by_evidence: dict[str, list[dict[str, Any]]],
    overviews_by_evidence: dict[str, list[dict[str, Any]]],
) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for row in rows:
        session = dict(row)
        if not _session_path_is_healthy(Path(session["session_path"])):
            continue
        scenario_rows = scenarios_by_evidence.get(str(session["evidence_id"]), [])
        session["scenario_ids"] = [scenario["scenario_id"] for scenario in scenario_rows]
        session["passed_scenarios"] = sum(
            1 for scenario in scenario_rows if scenario["overall_pass"] == 1
        )
        session["failed_scenarios"] = sum(
            1 for scenario in scenario_rows if scenario["overall_pass"] == 0
        )
        session["overview_pngs"] = overviews_by_evidence.get(str(session["evidence_id"]), [])
        session["overview_png"] = session["overview_pngs"][0] if session["overview_pngs"] else None

        scenario_count = len(scenario_rows)
        if session["failed_scenarios"]:
            outcome = "failed"
        elif scenario_count and session["passed_scenarios"] == scenario_count:
            outcome = "passed"
        else:
            outcome = "unknown"
        mode = _session_list_mode(session)
        if not session["scenario_ids"]:
            scenario = "-"
        elif scenario_count <= 2:
            scenario = ", ".join(session["scenario_ids"])
        else:
            scenario = f"{session['scenario_ids'][0]} +{scenario_count - 1}"
        source, source_label = _session_list_source(session.get("source"))
        worktree = _session_list_worktree(session, source_label)
        result_display = (
            f"{session['passed_scenarios']}/{scenario_count} 通过"
            if scenario_count > 1
            else "通过"
            if session["passed_scenarios"]
            else "不通过"
            if session["failed_scenarios"]
            else "-"
        )
        search_values = [
            session["evidence_id"],
            session["session_id"],
            scenario,
            *session["scenario_ids"],
            source_label,
            session.get("source"),
            session["suite"],
            mode,
            _MODE_LABELS[mode],
            worktree,
            session.get("worktree_name"),
            session.get("branch"),
            outcome,
            _OUTCOME_LABELS[outcome],
            result_display,
        ]
        session.update(
            {
                "_outcome": outcome,
                "_time": str(session.get("created_at") or session.get("ended_at") or session["session_id"]),
                "_mode": mode,
                "_scenario": scenario,
                "_source": source,
                "_source_label": source_label,
                "_worktree": worktree,
                "_search": "\n".join(str(value or "").casefold() for value in search_values),
            }
        )
        records.append(session)
    return records


def _session_list_facets(eligible: list[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    fields = {
        "result": ("_outcome", _OUTCOME_LABELS),
        "scenarioCount": ("scenario_ids", None),
        "mode": ("_mode", _MODE_LABELS),
        "scenario": ("_scenario", None),
        "source": ("_source", _SOURCE_LABELS),
        "worktree": ("_worktree", None),
    }
    facets: dict[str, list[dict[str, Any]]] = {}
    for facet_name, (field, labels) in fields.items():
        values = (
            [str(len(record[field])) for record in eligible]
            if facet_name == "scenarioCount"
            else [str(record[field]) for record in eligible if record[field] != ""]
        )
        counts = Counter(values)
        ordered = sorted(counts, key=int if facet_name == "scenarioCount" else str.casefold)
        facets[facet_name] = [
            {
                "value": value,
                "label": labels.get(value, value) if labels else value,
                "count": counts[value],
            }
            for value in ordered
        ]
    return facets


def _filter_and_page_session_records(
    eligible: list[dict[str, Any]],
    query: EvidenceSessionListQuery,
) -> tuple[list[dict[str, Any]], int, int]:
    search = query.search.strip().casefold()
    filtered = [
        record
        for record in eligible
        if (not search or search in record["_search"])
        and (query.result is None or record["_outcome"] == query.result)
        and (query.scenario_count is None or len(record["scenario_ids"]) == query.scenario_count)
        and (query.mode is None or record["_mode"] == query.mode)
        and (query.scenario is None or record["_scenario"] == query.scenario)
        and (query.source is None or record["_source"] == query.source)
        and (query.worktree is None or record["_worktree"] == query.worktree)
    ]
    sort_keys: dict[str, Callable[[dict[str, Any]], Any]] = {
        "time": lambda record: _absolute_time_sort_key(record["_time"]),
        "result": lambda record: _OUTCOME_SORT_ORDER[record["_outcome"]],
        "scenarioCount": lambda record: len(record["scenario_ids"]),
        "mode": lambda record: _MODE_SORT_ORDER[record["_mode"]],
        "scenario": lambda record: _natural_text_sort_key(record["_scenario"]),
        "source": lambda record: _natural_text_sort_key(record["_source_label"]),
        "worktree": lambda record: _natural_text_sort_key(record["_worktree"]),
    }
    filtered.sort(key=lambda record: str(record["evidence_id"]))
    filtered.sort(key=sort_keys[query.sort_key], reverse=query.sort_direction == "desc")
    total_pages = max(1, ceil(len(filtered) / query.page_size))
    normalized_page = min(max(query.page, 1), total_pages)
    offset = (normalized_page - 1) * query.page_size
    return filtered[offset : offset + query.page_size], len(filtered), normalized_page


def list_sessions(
    query: EvidenceSessionListQuery = EvidenceSessionListQuery(),
    repo_root: Path | None = None,
) -> dict[str, Any]:
    config = load_effective_config(repo_root=repo_root)
    with closing(open_initialized(config)) as conn:
        rows = conn.execute(
            "select * from sessions",
        ).fetchall()
        scenario_rows = conn.execute(
            "select evidence_id, scenario_id, overall_pass from scenarios order by evidence_id, scenario_id",
        ).fetchall()
        overview_rows = conn.execute(
            """
            select evidence_id, scenario_id, relative_path
            from artifacts
            where kind = 'trajectory_dashboard_png' and available = 1
            order by evidence_id, scenario_id
            """,
        ).fetchall()
        scenarios_by_evidence: dict[str, list[dict[str, Any]]] = {}
        for row in scenario_rows:
            scenarios_by_evidence.setdefault(str(row["evidence_id"]), []).append(dict(row))
        overviews_by_evidence: dict[str, list[dict[str, Any]]] = {}
        for row in overview_rows:
            overview = dict(row)
            evidence_id = str(overview.pop("evidence_id"))
            overviews_by_evidence.setdefault(evidence_id, []).append(overview)

        eligible = _build_session_list_records(rows, scenarios_by_evidence, overviews_by_evidence)
        facets = _session_list_facets(eligible)
        page_records, filtered_total, normalized_page = _filter_and_page_session_records(eligible, query)
        if any(
            not _session_path_is_healthy(Path(session["session_path"]))
            for session in page_records
        ):
            eligible = _build_session_list_records(rows, scenarios_by_evidence, overviews_by_evidence)
            facets = _session_list_facets(eligible)
            page_records, filtered_total, normalized_page = _filter_and_page_session_records(eligible, query)

        for session in page_records:
            session_path = Path(session["session_path"])
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
            for private_field in [field for field in session if field.startswith("_")]:
                session.pop(private_field)

        return {
            "sessions": page_records,
            "total": len(eligible),
            "filtered_total": filtered_total,
            "page": normalized_page,
            "page_size": query.page_size,
            "total_pages": max(1, ceil(filtered_total / query.page_size)),
            "facets": facets,
        }


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


def _delete_evidence_session_locked(
    evidence_id: str,
    config: EvidenceLibraryConfig,
    repo_root: Path | None = None,
) -> dict[str, Any]:
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
        try:
            _recover_indexed_deletion(config, evidence_id, target)
        except (OSError, PermissionError) as exc:
            raise PermissionError("Interrupted evidence deletion recovery failed") from exc

        if not target.exists():
            _delete_evidence_rows(conn, evidence_id)
            conn.commit()
            return {
                "evidence_id": evidence_id,
                "deleted_path": str(resolved_target),
                "filesystem_deleted": False,
                "filesystem_cleanup": "not_needed",
            }

        staging_dir, staged_target, metadata_path, recovery_record_path = _stage_deletion_target(
            evidence_id, target, config
        )
        try:
            _delete_evidence_rows(conn, evidence_id)
            conn.commit()
        except Exception:
            try:
                conn.rollback()
            except sqlite3.Error:
                pass
            try:
                _restore_staged_target(
                    staging_dir,
                    staged_target,
                    metadata_path,
                    recovery_record_path,
                    evidence_id=evidence_id,
                    target=target,
                )
            except OSError as restore_error:
                raise RuntimeError("Evidence deletion target restoration failed") from restore_error
            raise

    try:
        shutil.rmtree(staged_target)
        metadata_path.unlink(missing_ok=True)
        recovery_record_path.unlink(missing_ok=True)
        _temporary_metadata_path(recovery_record_path).unlink(missing_ok=True)
    except Exception:
        return {
            "evidence_id": evidence_id,
            "deleted_path": str(resolved_target),
            "filesystem_deleted": False,
            "filesystem_cleanup": "pending",
            "cleanup_error": "staged filesystem cleanup is pending",
            "cleanup_path": str(staged_target),
            "cleanup_metadata_path": str(metadata_path),
            "cleanup_paths": [
                str(staged_target),
                str(metadata_path),
                str(recovery_record_path),
            ],
        }

    _remove_empty_staging_dir(staging_dir)
    _remove_empty_recovery_dir(config)
    return {
        "evidence_id": evidence_id,
        "deleted_path": str(resolved_target),
        "filesystem_deleted": True,
        "filesystem_cleanup": "completed",
    }


def delete_evidence_session(evidence_id: str, repo_root: Path | None = None) -> dict[str, Any]:
    config = load_effective_config(repo_root=repo_root)
    with _deletion_scan_coordination_lock(config, exclusive=False):
        with _evidence_deletion_lock(config, evidence_id):
            return _delete_evidence_session_locked(evidence_id, config, repo_root)


def delete_evidence_sessions(evidence_ids: list[str], repo_root: Path | None = None) -> dict[str, Any]:
    results: list[dict[str, Any]] = []
    for evidence_id in evidence_ids:
        try:
            deleted = delete_evidence_session(evidence_id, repo_root=repo_root)
        except (LookupError, PermissionError) as exc:
            results.append({"evidence_id": evidence_id, "status": "failed", "error": str(exc)})
        except sqlite3.Error:
            results.append({
                "evidence_id": evidence_id,
                "status": "failed",
                "error": "database operation failed",
            })
        except OSError:
            results.append({
                "evidence_id": evidence_id,
                "status": "failed",
                "error": "filesystem operation failed",
            })
        except Exception:
            results.append({
                "evidence_id": evidence_id,
                "status": "failed",
                "error": "evidence deletion failed",
            })
        else:
            results.append({**deleted, "status": "deleted"})
    deleted_count = sum(item["status"] == "deleted" for item in results)
    return {
        "requested": len(evidence_ids),
        "deleted": deleted_count,
        "failed": len(results) - deleted_count,
        "results": results,
    }
