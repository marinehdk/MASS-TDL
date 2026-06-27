from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class ScenarioMatrixRow:
    scenario_id: str
    overall_pass: bool
    families: set[str]


@dataclass(frozen=True)
class BatchMatrix:
    path: Path
    rows: dict[str, ScenarioMatrixRow]

    @property
    def pass_count(self) -> int:
        return sum(1 for row in self.rows.values() if row.overall_pass)

    @property
    def total_count(self) -> int:
        return len(self.rows)


@dataclass(frozen=True)
class RegressionReport:
    baseline_pass_count: int
    current_pass_count: int
    regressed_scenarios: list[str]
    improved_scenarios: list[str]


def _families(result: dict[str, Any]) -> set[str]:
    families: set[str] = set()
    if result.get("cpa_ok") is False:
        families.add("CPA")
    if result.get("stability_pass") is False:
        families.add("Stability")

    route_required = bool(result.get("route_return_required", True))
    if route_required and result.get("returned_to_route") is False:
        families.add("RouteReturn")
    if result.get("route_corridor_ok") is False:
        families.add("Corridor")
    if result.get("overtake_required") and result.get("overtake_completed") is False:
        families.add("Overtake")

    domain = result.get("domain_gates") or {}
    if domain.get("risk_gate_ok") is False:
        families.add("Risk")
    if domain.get("seamanship_gate_ok") is False:
        families.add("Seamanship")

    phase = result.get("phase_semantics") or {}
    if phase.get("phase_semantics_ok") is False:
        families.add("Phase")
    return families


def _iter_scenario_results(data: Any) -> list[tuple[str, dict[str, Any]]]:
    if isinstance(data, dict):
        if isinstance(data.get("scenarios"), list):
            return _iter_scenario_results(data["scenarios"])
        rows: list[tuple[str, dict[str, Any]]] = []
        for scenario_id, result in data.items():
            if not isinstance(result, dict):
                continue
            sid = result.get("scenario_id") or result.get("scenario") or scenario_id
            if "overall_pass" in result or "pass" in result:
                rows.append((str(sid), result))
        return rows

    if isinstance(data, list):
        rows = []
        for result in data:
            if not isinstance(result, dict):
                continue
            sid = result.get("scenario_id") or result.get("scenario") or result.get("id")
            if sid and ("overall_pass" in result or "pass" in result):
                rows.append((str(sid), result))
        return rows

    return []


def summarize_batch(path: Path | str) -> BatchMatrix:
    batch_path = Path(path)
    data = json.loads(batch_path.read_text(encoding="utf-8"))
    rows = {
        scenario_id: ScenarioMatrixRow(
            scenario_id=scenario_id,
            overall_pass=bool(result.get("overall_pass", result.get("pass", False))),
            families=_families(result),
        )
        for scenario_id, result in _iter_scenario_results(data)
    }
    return BatchMatrix(path=batch_path, rows=rows)


def compare_batches(baseline: BatchMatrix, current: BatchMatrix) -> RegressionReport:
    regressed: list[str] = []
    improved: list[str] = []
    for scenario_id, base_row in baseline.rows.items():
        current_row = current.rows.get(scenario_id)
        if current_row is None:
            continue
        if base_row.overall_pass and not current_row.overall_pass:
            regressed.append(scenario_id)
        if not base_row.overall_pass and current_row.overall_pass:
            improved.append(scenario_id)
    return RegressionReport(
        baseline_pass_count=baseline.pass_count,
        current_pass_count=current.pass_count,
        regressed_scenarios=sorted(regressed),
        improved_scenarios=sorted(improved),
    )
