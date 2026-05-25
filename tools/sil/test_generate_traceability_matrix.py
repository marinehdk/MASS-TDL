"""Tests for generate_traceability_matrix.py"""
from __future__ import annotations

import csv
from pathlib import Path

import pytest
import yaml

from generate_traceability_matrix import (
    parse_scenario,
    scan_scenarios,
    write_csv,
    print_summary,
)


def _write_yaml(tmp: Path, name: str, data: dict) -> Path:
    fp = tmp / name
    fp.parent.mkdir(parents=True, exist_ok=True)
    fp.write_text(yaml.dump(data, default_flow_style=False), encoding="utf-8")
    return fp


def _make_scenario(
    scenario_id: str = "test-01-v1.0",
    rule: str = "Rule14",
    colregs_rules: list[str] | None = None,
    odd_domain: str = "open_sea",
    sog: float = 10.0,
    target_count: int = 1,
    wind_mps: float = 0.0,
    vis_nm: float = 10.0,
    seed: int | None = None,
) -> dict:
    targets = []
    for i in range(target_count):
        targets.append({
            "id": f"ts{i+1}",
            "static": {"id": i + 2},
            "initial": {
                "position": {"latitude": 63.5, "longitude": 10.4},
                "cog": 180.0,
                "sog": 10.0,
                "heading": 180.0,
            },
        })
    meta: dict = {
        "scenario_id": scenario_id,
        "odd_cell": {"domain": odd_domain},
        "encounter": {"rule": rule},
    }
    if colregs_rules:
        meta["colregs_rules"] = colregs_rules
    if seed is not None:
        meta["seed"] = seed
    return {
        "ownShip": {
            "initial": {"sog": sog},
        },
        "targetShips": targets,
        "environment": {
            "wind": {"speed_mps": wind_mps},
            "visibility_nm": vis_nm,
        },
        "metadata": meta,
    }


class TestParseScenario:
    def test_basic_single_rule(self, tmp_path: Path) -> None:
        data = _make_scenario(scenario_id="ho-01", rule="Rule14", sog=12.0, target_count=1)
        fp = _write_yaml(tmp_path, "ho-01.yaml", data)
        rows = parse_scenario(fp)
        assert len(rows) == 1
        assert rows[0]["scenario_id"] == "ho-01"
        assert rows[0]["rule_id"] == "Rule14"
        assert rows[0]["odd_zone"] == "open_sea"
        assert rows[0]["target_count"] == 1
        assert rows[0]["own_sog_kn"] == 12.0

    def test_colregs_rules_list(self, tmp_path: Path) -> None:
        data = _make_scenario(
            scenario_id="ot-01",
            rule="Rule13",
            colregs_rules=["R13", "R16", "R8"],
        )
        fp = _write_yaml(tmp_path, "ot-01.yaml", data)
        rows = parse_scenario(fp)
        assert len(rows) == 3
        rule_ids = [r["rule_id"] for r in rows]
        assert "Rule13" in rule_ids
        assert "Rule16" in rule_ids
        assert "Rule8" in rule_ids

    def test_no_duplicate_rules(self, tmp_path: Path) -> None:
        data = _make_scenario(
            scenario_id="ot-02",
            rule="Rule13",
            colregs_rules=["R13", "R16"],
        )
        fp = _write_yaml(tmp_path, "ot-02.yaml", data)
        rows = parse_scenario(fp)
        rule_ids = [r["rule_id"] for r in rows]
        assert rule_ids.count("Rule13") == 1

    def test_missing_fields_use_defaults(self, tmp_path: Path) -> None:
        data = {"metadata": {"encounter": {"rule": "Rule5"}}}
        fp = _write_yaml(tmp_path, "minimal.yaml", data)
        rows = parse_scenario(fp)
        assert len(rows) == 1
        assert rows[0]["rule_id"] == "Rule5"
        assert rows[0]["odd_zone"] == "unknown"
        assert rows[0]["target_count"] == 0
        assert rows[0]["own_sog_kn"] == 0.0

    def test_scenario_id_from_filename(self, tmp_path: Path) -> None:
        data = {"metadata": {"encounter": {"rule": "Rule14"}}}
        fp = _write_yaml(tmp_path, "my-scenario.yaml", data)
        rows = parse_scenario(fp)
        assert rows[0]["scenario_id"] == "my-scenario"

    def test_wind_conversion(self, tmp_path: Path) -> None:
        data = _make_scenario(wind_mps=10.0)
        fp = _write_yaml(tmp_path, "wind.yaml", data)
        rows = parse_scenario(fp)
        assert rows[0]["wind_kn"] == pytest.approx(19.44, abs=0.1)

    def test_visibility_conversion(self, tmp_path: Path) -> None:
        data = _make_scenario(vis_nm=5.4)
        fp = _write_yaml(tmp_path, "vis.yaml", data)
        rows = parse_scenario(fp)
        assert rows[0]["vis_m"] == pytest.approx(5.4 * 1852.0, abs=1.0)

    def test_seed_extraction(self, tmp_path: Path) -> None:
        data = _make_scenario(seed=3)
        fp = _write_yaml(tmp_path, "seeded.yaml", data)
        rows = parse_scenario(fp)
        assert rows[0]["seed"] == 3

    def test_multi_target(self, tmp_path: Path) -> None:
        data = _make_scenario(target_count=3)
        fp = _write_yaml(tmp_path, "multi.yaml", data)
        rows = parse_scenario(fp)
        assert rows[0]["target_count"] == 3

    def test_skips_dotfiles(self, tmp_path: Path) -> None:
        data = _make_scenario()
        _write_yaml(tmp_path, ".hidden.yaml", data)
        rows = scan_scenarios(tmp_path)
        assert len(rows) == 0

    def test_non_dict_yaml_returns_empty(self, tmp_path: Path) -> None:
        fp = tmp_path / "list.yaml"
        fp.write_text("- item1\n- item2\n", encoding="utf-8")
        rows = parse_scenario(fp)
        assert rows == []


class TestWriteCsv:
    def test_csv_format(self, tmp_path: Path) -> None:
        rows = [
            {
                "scenario_id": "s1",
                "rule_id": "Rule14",
                "odd_zone": "open_sea",
                "target_count": 1,
                "own_sog_kn": 10.0,
                "wind_kn": 0.0,
                "vis_m": 18520.0,
                "seed": 1,
                "source": "test",
                "file": "/tmp/s1.yaml",
            },
            {
                "scenario_id": "s2",
                "rule_id": "Rule15",
                "odd_zone": "coastal",
                "target_count": 2,
                "own_sog_kn": 12.0,
                "wind_kn": 5.0,
                "vis_m": 9260.0,
                "seed": 2,
                "source": "test",
                "file": "/tmp/s2.yaml",
            },
        ]
        out = tmp_path / "out.csv"
        write_csv(rows, out)

        with open(out, newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            data = list(reader)

        assert len(data) == 2
        assert data[0]["scenario_id"] == "s1"
        assert data[0]["rule_id"] == "Rule14"
        assert data[1]["target_count"] == "2"

    def test_csv_creates_parent_dirs(self, tmp_path: Path) -> None:
        out = tmp_path / "sub" / "dir" / "matrix.csv"
        write_csv([], out)
        assert out.exists()


class TestScanScenarios:
    def test_recursive_scan(self, tmp_path: Path) -> None:
        d1 = _make_scenario(scenario_id="a1", rule="Rule14")
        d2 = _make_scenario(scenario_id="a2", rule="Rule15")
        _write_yaml(tmp_path / "group1", "s1.yaml", d1)
        _write_yaml(tmp_path / "group2", "s2.yaml", d2)

        rows = scan_scenarios(tmp_path)
        assert len(rows) == 2
        ids = {r["scenario_id"] for r in rows}
        assert "a1" in ids
        assert "a2" in ids


class TestPrintSummary:
    def test_summary_runs_without_error(self, tmp_path: Path) -> None:
        rows = [
            {
                "scenario_id": "s1",
                "rule_id": "Rule14",
                "odd_zone": "open_sea",
                "target_count": 1,
                "own_sog_kn": 10.0,
                "wind_kn": 0.0,
                "vis_m": 18520.0,
                "seed": 1,
                "source": "test",
                "file": "/tmp/s1.yaml",
            },
        ]
        print_summary(rows)
