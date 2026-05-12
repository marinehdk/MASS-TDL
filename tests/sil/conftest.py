"""Pytest configuration and fixtures for SIL tests."""
from __future__ import annotations

import json
from collections.abc import Generator
from pathlib import Path

import pytest

from tools.sil.scenario_spec import ScenarioSpec


@pytest.fixture(scope="session")
def imazu22_collector() -> Generator[dict, None, None]:
    """Collect all imazu-22 scenario results for the entire test session.

    Writes test-results/imazu22_results.json on teardown.
    """
    results: dict = {
        "schema_version": "1.0",
        "collection": "imazu22",
        "artifacts": [],
    }

    yield results

    out_path = Path("test-results/imazu22_results.json")
    out_path.parent.mkdir(parents=True, exist_ok=True)

    # Compute aggregate metrics across all 22 scenarios
    total = len(results["artifacts"])
    pass_count = sum(1 for a in results["artifacts"] if a["result"] == "PASS")
    cpa_ge_200 = sum(
        1 for a in results["artifacts"]
        if a["metrics"].get("dcpa_with_action_m", 0) >= 200.0
    )
    results["total_scenarios"] = total
    results["pass_count"] = pass_count
    results["e1_1_pass_pct"] = round(pass_count / total * 100, 2)
    results["cpa_ge_200m_pct"] = round(cpa_ge_200 / total * 100, 2)
    # COLREG classification rate: mock yields 0% until real classifier integrated
    results["colreg_classification_rate_pct"] = 0.0

    out_path.write_text(json.dumps(results, indent=2))


@pytest.fixture
def imazu_yaml(request) -> Path:
    """Return path to an imazu22 YAML file based on the requesting test param."""
    scenario_dir = Path(__file__).parent.parent.parent / "scenarios" / "imazu22"
    return scenario_dir / request.param


@pytest.fixture
def imazu_spec(imazu_yaml: Path) -> ScenarioSpec:
    """Parse a scenario YAML file into a ScenarioSpec."""
    return ScenarioSpec.from_file(imazu_yaml)