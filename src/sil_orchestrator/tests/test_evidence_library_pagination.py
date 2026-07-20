from __future__ import annotations

import json
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest

from sil_orchestrator.evidence_library import service
from sil_orchestrator.evidence_library.service import (
    EvidenceSessionListQuery,
    list_sessions,
    open_initialized,
)


MODE_SUITES = {
    "debug": "debug",
    "cohort": "cohort",
    "full": "clean12",
    "avoidance": "single",
}
MODE_LABELS = {
    "debug": "调试验证",
    "cohort": "同类验证",
    "full": "完整验证",
    "avoidance": "避碰验证",
}


@dataclass
class PagedLibrary:
    repo: Path
    session_paths: list[Path]
    sort_values: dict[str, dict[str, str | int]]

    def remove_session_path(self, index: int) -> None:
        (self.session_paths[index] / "manifest.json").unlink()


@pytest.fixture
def paged_library(tmp_path, monkeypatch) -> PagedLibrary:
    repo = tmp_path / "repo"
    root = repo / "runs" / "trace_eval"
    config_home = tmp_path / "config"
    monkeypatch.setenv("MASS_L3_CONFIG_HOME", str(config_home))
    session_paths: list[Path] = []
    sort_values: dict[str, dict[str, str | int]] = {}
    session_rows = []
    scenario_rows = []
    base_time = datetime(2026, 7, 1, tzinfo=timezone.utc)

    for index in range(313):
        evidence_id = f"evidence-{index:03d}"
        mode = tuple(MODE_SUITES)[index % len(MODE_SUITES)]
        suite = MODE_SUITES[mode]
        source = "cli" if index % 2 else "frontend"
        session_id = f"session-{index:03d}"
        session_path = root / session_id
        session_path.mkdir(parents=True)
        (session_path / "manifest.json").write_text(json.dumps({"session_name": session_id}))
        session_paths.append(session_path)

        created_at = (base_time + timedelta(minutes=index // 2)).isoformat()
        scenario_count = index % 3
        session_rows.append(
            (
                evidence_id,
                session_id,
                source,
                suite,
                "primary",
                f"tree-{'a' if index % 4 == 1 else 'b'}" if source == "cli" else None,
                f"branch-{index % 3}",
                str(session_path),
                created_at,
                created_at,
                "completed",
                1,
                scenario_count,
                "ok",
                None,
                "keep",
                float(index),
            )
        )
        scenario_ids = []
        for scenario_index in range(scenario_count):
            scenario_id = (
                "rare-rule"
                if index == 1
                else "colreg-rule15-cs"
                if index % 5 == 0 and scenario_index == 0
                else "colreg-rule14-ho"
                if scenario_index == 0
                else f"extra-{index:03d}"
            )
            overall_pass = 1 if index % 2 == 0 else (0 if scenario_index == 0 else 1)
            scenario_ids.append(scenario_id)
            scenario_rows.append(
                (evidence_id, session_id, scenario_id, "pass" if overall_pass else "fail", overall_pass)
            )

        outcome = "unknown" if scenario_count == 0 else "passed" if index % 2 == 0 else "failed"
        scenario_label = (
            "-"
            if not scenario_ids
            else ", ".join(scenario_ids)
            if len(scenario_ids) <= 2
            else f"{scenario_ids[0]} +{len(scenario_ids) - 1}"
        )
        worktree = (
            ""
            if source == "frontend"
            else f"tree-{'a' if index % 4 == 1 else 'b'}"
        )
        sort_values[evidence_id] = {
            "time": created_at,
            "result": {"unknown": "-", "failed": "不通过", "passed": "通过"}[outcome],
            "scenarioCount": scenario_count,
            "mode": MODE_LABELS[mode],
            "scenario": scenario_label,
            "source": "CLI" if source == "cli" else "Front",
            "worktree": worktree,
        }

    with open_initialized() as conn:
        conn.executemany(
            """
            insert into sessions (
              evidence_id, session_id, source, suite, root_id, worktree_name, branch,
              session_path, created_at, ended_at, status, valid_data, scenario_count,
              ingest_status, ingest_error, raw_trace_policy, latest_mtime
            ) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            session_rows,
        )
        conn.executemany(
            """
            insert into scenarios (
              evidence_id, session_id, scenario_id, verdict, overall_pass
            ) values (?, ?, ?, ?, ?)
            """,
            scenario_rows,
        )
        conn.commit()

    return PagedLibrary(repo=repo, session_paths=session_paths, sort_values=sort_values)


def test_lists_exact_total_without_global_cap(paged_library):
    result = list_sessions(
        EvidenceSessionListQuery(page=1, page_size=20),
        repo_root=paged_library.repo,
    )

    assert result["total"] == 313
    assert result["filtered_total"] == 313
    assert result["total_pages"] == 16
    assert len(result["sessions"]) == 20


def test_lists_last_page_and_fifty_row_pages(paged_library):
    last = list_sessions(EvidenceSessionListQuery(page=16), repo_root=paged_library.repo)
    fifty = list_sessions(
        EvidenceSessionListQuery(page=7, page_size=50),
        repo_root=paged_library.repo,
    )

    assert len(last["sessions"]) == 13
    assert fifty["total_pages"] == 7
    assert len(fifty["sessions"]) == 13


def test_excludes_unhealthy_paths_and_normalizes_page(paged_library):
    paged_library.remove_session_path(0)

    result = list_sessions(
        EvidenceSessionListQuery(page=99, page_size=50),
        repo_root=paged_library.repo,
    )

    assert result["total"] == 312
    assert result["page"] == 7
    assert len(result["sessions"]) == 12


@pytest.mark.parametrize(
    ("query", "expected_total", "field", "value"),
    [
        (EvidenceSessionListQuery(result="failed"), 104, "failed_scenarios", 1),
        (EvidenceSessionListQuery(scenario_count=2), 104, "scenario_ids", 2),
        (EvidenceSessionListQuery(mode="avoidance"), 78, "suite", "single"),
        (EvidenceSessionListQuery(scenario="rare-rule"), 1, "scenario_ids", "rare-rule"),
        (EvidenceSessionListQuery(source="cli"), 156, "source", "cli"),
        (EvidenceSessionListQuery(worktree="tree-a"), 78, "worktree_name", "tree-a"),
    ],
)
def test_filters_complete_dataset(paged_library, query, expected_total, field, value):
    result = list_sessions(query, repo_root=paged_library.repo)

    assert result["filtered_total"] == expected_total
    assert result["sessions"]
    if field == "scenario_ids" and isinstance(value, int):
        assert all(len(row[field]) == value for row in result["sessions"])
    elif field == "scenario_ids":
        assert all(value in row[field] for row in result["sessions"])
    else:
        assert all(row[field] == value for row in result["sessions"])


@pytest.mark.parametrize(
    ("search", "expected_total", "expected_field", "expected_value"),
    [
        ("完整验证", 78, "suite", "clean12"),
        ("full", 78, "suite", "clean12"),
        ("不通过", 104, "failed_scenarios", 1),
        ("failed", 104, "failed_scenarios", 1),
        ("Front", 157, "source", "frontend"),
        ("frontend", 157, "source", "frontend"),
        ("rare-rule", 1, "scenario_ids", "rare-rule"),
        ("tree-a", 78, "worktree_name", "tree-a"),
    ],
)
def test_searches_raw_and_localized_visible_fields(
    paged_library,
    search,
    expected_total,
    expected_field,
    expected_value,
):
    result = list_sessions(
        EvidenceSessionListQuery(search=search),
        repo_root=paged_library.repo,
    )

    assert result["filtered_total"] == expected_total
    if expected_field == "scenario_ids":
        assert all(expected_value in row[expected_field] for row in result["sessions"])
    else:
        assert all(row[expected_field] == expected_value for row in result["sessions"])


@pytest.mark.parametrize(
    "sort_key",
    ["time", "result", "scenarioCount", "mode", "scenario", "source", "worktree"],
)
@pytest.mark.parametrize("sort_direction", ["asc", "desc"])
def test_sorts_every_key_across_pages_with_fixed_ascending_tie_break(
    paged_library,
    sort_key,
    sort_direction,
):
    pages = [
        list_sessions(
            EvidenceSessionListQuery(
                page=page,
                sort_key=sort_key,
                sort_direction=sort_direction,
            ),
            repo_root=paged_library.repo,
        )
        for page in (1, 2)
    ]
    actual_ids = [row["evidence_id"] for result in pages for row in result["sessions"]]
    expected_ids = sorted(paged_library.sort_values)
    expected_ids.sort(
        key=lambda evidence_id: paged_library.sort_values[evidence_id][sort_key],
        reverse=sort_direction == "desc",
    )

    assert actual_ids == expected_ids[:40]
    primary_values = [paged_library.sort_values[evidence_id][sort_key] for evidence_id in actual_ids]
    assert primary_values == sorted(primary_values, reverse=sort_direction == "desc")
    equal_key_pairs = [
        (left, right)
        for left, right in zip(actual_ids, actual_ids[1:])
        if paged_library.sort_values[left][sort_key]
        == paged_library.sort_values[right][sort_key]
    ]
    assert equal_key_pairs
    assert all(left < right for left, right in equal_key_pairs)


def test_rebuilds_page_when_path_disappears_after_initial_eligibility(
    paged_library,
    monkeypatch,
):
    target_path = paged_library.session_paths[0]
    target_checks = 0
    real_health_check = service._session_path_is_healthy

    def disappearing_health_check(session_path: Path) -> bool:
        nonlocal target_checks
        if session_path == target_path:
            target_checks += 1
            if target_checks == 2:
                (target_path / "manifest.json").unlink()
        return real_health_check(session_path)

    monkeypatch.setattr(service, "_session_path_is_healthy", disappearing_health_check)

    result = list_sessions(
        EvidenceSessionListQuery(page=99, page_size=50, source="front"),
        repo_root=paged_library.repo,
    )

    assert target_checks >= 3
    assert result["total"] == 312
    assert result["filtered_total"] == 156
    assert result["page"] == 4
    assert result["total_pages"] == 4
    assert len(result["sessions"]) == 6
    assert all(row["evidence_id"] != "evidence-000" for row in result["sessions"])


def test_facets_cover_values_absent_from_current_page(paged_library):
    result = list_sessions(EvidenceSessionListQuery(page=1), repo_root=paged_library.repo)

    assert all("rare-rule" not in row["scenario_ids"] for row in result["sessions"])
    assert {item["value"] for item in result["facets"]["scenario"]} >= {"rare-rule"}
    rare = next(item for item in result["facets"]["scenario"] if item["value"] == "rare-rule")
    assert rare == {"value": "rare-rule", "label": "rare-rule", "count": 1}


def test_empty_filter_result_still_has_one_page(paged_library):
    result = list_sessions(
        EvidenceSessionListQuery(search="no-such-evidence-session"),
        repo_root=paged_library.repo,
    )

    assert result["sessions"] == []
    assert result["total"] == 313
    assert result["filtered_total"] == 0
    assert result["page"] == 1
    assert result["total_pages"] == 1
