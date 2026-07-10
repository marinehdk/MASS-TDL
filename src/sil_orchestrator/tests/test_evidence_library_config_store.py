from __future__ import annotations

import sqlite3
from pathlib import Path
import hashlib

from sil_orchestrator.evidence_library.config import load_effective_config
from sil_orchestrator.evidence_library.store import (
    compute_evidence_id,
    initialize_schema,
    open_database,
)


def test_load_effective_config_uses_mass_l3_config_home(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    repo.mkdir()
    config_home = tmp_path / "config-home"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))

    config = load_effective_config(repo_root=repo)

    assert config.config_home == config_home
    assert config.database_path == config_home / "evidence_index.sqlite"
    assert [root.root_id for root in config.roots] == [
        "primary-unified",
        "worktrees-unified",
        "primary",
        "worktrees",
    ]
    assert config.roots[0].path_glob == str(repo / "runs" / "*" / "trace")
    assert config.roots[1].path_glob == str(repo / ".worktrees" / "*" / "runs" / "*" / "trace")
    assert config.roots[2].path_glob == str(repo / "runs" / "trace_eval")
    assert config.roots[3].path_glob == str(repo / ".worktrees" / "*" / "runs" / "trace_eval")
    assert config.raw_trace_policy == "compress_after_ingest"
    assert config.effective_retention_policy == "keep"


def test_schema_has_replay_tables(tmp_path, monkeypatch):
    config_home = tmp_path / "config-home"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    config = load_effective_config(repo_root=tmp_path / "repo")

    conn = open_database(config)
    initialize_schema(conn)

    rows = conn.execute(
        "select name from sqlite_master where type='table' order by name"
    ).fetchall()
    names = {row[0] for row in rows}
    assert {
        "artifacts",
        "events",
        "gate_results",
        "roots",
        "scenarios",
        "sessions",
        "state_segments",
        "trajectory_downsample",
        "trajectory_samples",
    }.issubset(names)


def test_compute_evidence_id_uses_root_and_resolved_path(tmp_path):
    session_dir = tmp_path / "runs" / "trace_eval" / "same-session"
    session_dir.mkdir(parents=True)

    first = compute_evidence_id("primary", session_dir)
    second = compute_evidence_id("worktree-a", session_dir)

    assert len(first) == 64
    assert len(second) == 64
    assert first != second


def test_compute_evidence_id_matches_sha256_contract():
    session_path = Path("/tmp/evidence-library/task-1/sessionA")
    root_id = "primary"
    expected = "9087558c54a3d697005477b379905b5dbc173e9b78577c68c8818a839ed32840"

    assert compute_evidence_id(root_id, session_path) == expected
