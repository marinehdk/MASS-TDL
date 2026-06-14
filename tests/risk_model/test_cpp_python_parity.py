import json
from pathlib import Path

import pytest

from l3_risk_model import ColregsDuty, OwnShipInput, TargetInput, evaluate_target


FIXTURE_PATH = (
    Path(__file__).resolve().parents[2]
    / "src/l3_tdl_kernel/l3_risk_model/test/fixtures/risk_golden_cases.json"
)

EXPECTED_CPP_VALUES = {
    "forward_danger_closing": {
        "risk_phase": "Critical",
        "warning_margin_m": -425.600,
        "danger_margin_m": -112.000,
        "warning_ddv": 0.603174603,
        "danger_ddv": 0.285714286,
        "risk_score": 0.668589085,
    },
    "starboard_warning_crossing": {
        "risk_phase": "Warning",
        "warning_margin_m": -73.770624939,
        "danger_margin_m": 209.487480431,
        "warning_ddv": 0.115749360,
        "danger_ddv": 0.000,
        "risk_score": 0.414458439,
    },
    "opening_clear": {
        "risk_phase": "Clear",
        "warning_margin_m": 645.681649249,
        "danger_margin_m": 778.000077674,
        "warning_ddv": 0.000,
        "danger_ddv": 0.000,
        "risk_score": 0.005,
    },
}


def test_python_risk_model_matches_cpp_golden_values() -> None:
    cases = json.loads(FIXTURE_PATH.read_text())

    for case in cases:
        risk = evaluate_target(
            OwnShipInput(**case["own"]),
            TargetInput(**case["target"]),
            ColregsDuty(case["duty"]),
        )
        expected = EXPECTED_CPP_VALUES[case["case_id"]]

        assert risk.risk_phase.value == expected["risk_phase"]
        assert risk.warning_margin_m == pytest.approx(expected["warning_margin_m"], abs=1e-3)
        assert risk.danger_margin_m == pytest.approx(expected["danger_margin_m"], abs=1e-3)
        assert risk.warning_ddv == pytest.approx(expected["warning_ddv"], abs=1e-3)
        assert risk.danger_ddv == pytest.approx(expected["danger_ddv"], abs=1e-3)
        assert risk.risk_score == pytest.approx(expected["risk_score"], abs=1e-3)
