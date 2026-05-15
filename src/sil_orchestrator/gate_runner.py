"""6-Gate Sequencer — SIL preflight validation engine per Doc 3 §7.2.

Each gate is an independent async function returning GateResult.
GateRunner orchestrates sequential execution with per-gate timing.
"""
from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Callable, Awaitable, Any


@dataclass
class GateResult:
    gate_id: int
    passed: bool
    checks: list[str]
    duration_ms: float
    rationale: str

    def go_no_go(self) -> bool:
        return self.passed


@dataclass
class GateSpec:
    gate_id: int
    label: str
    handler: Callable[[], Awaitable[GateResult]]


_SIX_GATE_LABELS = {
    1: "System Readiness",
    2: "Module Health (M1-M8)",
    3: "Scenario Integrity",
    4: "ODD-Scenario Alignment",
    5: "Time Base & Evidence Chain",
    6: "Doer-Checker Independence",
}


class GateRunner:
    def __init__(self, scenario_id: str, scenario_data: dict | None = None):
        self.scenario_id = scenario_id
        self.scenario_data = scenario_data or {}
        self.gates: list[GateSpec] = []
        self._build_gates()

    def _build_gates(self) -> None:
        self.gates = [
            GateSpec(gate_id=gid, label=label, handler=self._stub_handler(gid))
            for gid, label in _SIX_GATE_LABELS.items()
        ]

    def _stub_handler(self, gate_id: int):
        async def stub() -> GateResult:
            return GateResult(
                gate_id=gate_id,
                passed=True,
                checks=[f"{_SIX_GATE_LABELS[gate_id]}: stub PASS"],
                duration_ms=0.0,
                rationale="stub — real gate not yet wired",
            )
        return stub

    async def run_all(self) -> list[GateResult]:
        results: list[GateResult] = []
        for spec in self.gates:
            t0 = time.monotonic()
            result = await spec.handler()
            result.duration_ms = (time.monotonic() - t0) * 1000
            results.append(result)
            if not result.passed:
                break
        return results

    def _gate_label_for(self, gate_id: int) -> str:
        return _SIX_GATE_LABELS.get(gate_id, f"Gate {gate_id}")
