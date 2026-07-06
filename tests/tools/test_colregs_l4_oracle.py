from tools.sil.colregs_module_oracle import evaluate_l4_oracle
from tools.sil.colregs_oracle_adapter import extract_l4_actuation


def _own(sim_t: float, heading_deg: float) -> dict:
    return {"topic": "/sil/own_ship_state", "sim_t": sim_t, "heading_deg": heading_deg}


def _gnc(sim_t: float, **overrides) -> dict:
    row = {
        "topic": "/l3/gnc/execution_status",
        "sim_t": sim_t,
        "execution_state": "ACCEPTED",
        "accepted": True,
        "command_source": "collision_avoidance",
        "plan_id": "m5-colregs-100000000000",
        "active_route_id": "m5-colregs-100000000000",
    }
    row.update(overrides)
    return row


def test_l4_oracle_uses_gnc_route_acceptance_for_waypoint_guidance():
    rows = [
        _own(100.0, 0.0),
        _gnc(102.0),
        _own(120.0, 2.0),
        _own(135.0, 5.0),
        _own(150.0, 12.0),
    ]

    l4 = extract_l4_actuation(rows, command_t=100.0, release_t=160.0)
    result = evaluate_l4_oracle(
        first_command_t=l4["first_command_t"],
        first_realized_t=l4["first_realized_t"],
        realized_heading_change_deg=l4["realized_heading_change_deg"],
        route_accepted=l4["route_accepted"],
        first_accepted_t=l4["first_accepted_t"],
    )

    assert result.passed
    assert result.evidence["handoff_delay_s"] == 2.0
    assert result.evidence["plant_response_delay_s"] > 20.0
    assert "ACTUATION_DELAY_EXCEEDED" not in result.failed_checks


def test_l4_oracle_flags_unaccepted_gnc_route():
    result = evaluate_l4_oracle(
        first_command_t=100.0,
        first_realized_t=130.0,
        realized_heading_change_deg=12.0,
        route_accepted=False,
        first_accepted_t=None,
    )

    assert not result.passed
    assert result.failed_checks == ["ROUTE_NOT_ACCEPTED"]


def test_l4_oracle_preserves_legacy_heading_delay_proxy_without_gnc_status():
    result = evaluate_l4_oracle(
        first_command_t=100.0,
        first_realized_t=130.0,
        realized_heading_change_deg=12.0,
    )

    assert not result.passed
    assert "ACTUATION_DELAY_EXCEEDED" in result.failed_checks
