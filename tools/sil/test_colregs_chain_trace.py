from tools.sil.colregs_chain_trace import build_chain_summary


def rec(t, topic, **fields):
    out = {"sim_t": t, "topic": topic}
    out.update(fields)
    return out


def test_detects_m5_status_flip_with_stable_upstream_inputs():
    records = [
        rec(1.0, "/l2/planned_route", route_hash="route-a"),
        rec(
            1.0,
            "/l3/m6/colregs_constraint",
            conflict_detected=True,
            colregs_chain_target_id="42",
            primary_preferred_direction="STARBOARD",
        ),
        rec(1.0, "/l3/m4/behavior_plan", behavior=2, rationale="COLREG_AVOID"),
        rec(
            1.0,
            "/l3/m5/avoidance_plan",
            status="NORMAL",
            rationale="solver_status=0",
            waypoints=[{"turn_radius_m": 300.0}],
        ),
        rec(
            2.0,
            "/l3/m6/colregs_constraint",
            conflict_detected=True,
            colregs_chain_target_id="42",
            primary_preferred_direction="STARBOARD",
        ),
        rec(2.0, "/l3/m4/behavior_plan", behavior=2, rationale="COLREG_AVOID"),
        rec(
            2.0,
            "/l3/m5/avoidance_plan",
            status="DEGRADED",
            rationale="M5 geometric COLREG fallback (solver_status=2)",
            waypoints=[{"turn_radius_m": 300.0}],
        ),
    ]
    summary = build_chain_summary(records)
    assert summary["route"]["hash_changes"] == 0
    assert summary["m6"]["conflict_toggles"] == 0
    assert summary["m4"]["behavior_toggles"] == 0
    assert summary["m5"]["status_transitions"] == ["NORMAL->DEGRADED"]
    assert summary["diagnosis"]["first_broken_stage"] == "M5"
    assert "stable upstream" in summary["diagnosis"]["reason"]


def test_detects_upstream_route_churn_before_m5_flip():
    records = [
        rec(1.0, "/l2/planned_route", route_hash="route-a"),
        rec(1.0, "/l3/m5/avoidance_plan", status="NORMAL", rationale="solver_status=0", waypoints=[]),
        rec(2.0, "/l2/planned_route", route_hash="route-b"),
        rec(
            2.0,
            "/l3/m5/avoidance_plan",
            status="DEGRADED",
            rationale="M5 geometric COLREG fallback (wrapped_heading_window)",
            waypoints=[{"turn_radius_m": 300.0}],
        ),
    ]
    summary = build_chain_summary(records)
    assert summary["route"]["hash_changes"] == 1
    assert summary["diagnosis"]["first_broken_stage"] == "L2"
    assert "route hash changed" in summary["diagnosis"]["reason"]


def test_lifecycle_release_while_m6_active_is_l4_lifecycle_fault():
    records = [
        rec(1.0, "/l3/m6/colregs_constraint", conflict_detected=True, colregs_chain_target_id="42"),
        rec(1.0, "/l3/m4/behavior_plan", behavior=2, rationale="COLREG_AVOID"),
        rec(1.0, "/l3/m5/avoidance_plan", status="DEGRADED", waypoints=[{"turn_radius_m": 300.0}]),
        rec(
            2.0,
            "/sil/lifecycle_status",
            avoidance_active=False,
            autopilot_enabled=True,
            valid_m5_plan=False,
            m5_plan_age_s=11.0,
        ),
    ]
    summary = build_chain_summary(records)
    assert summary["lifecycle"]["released_while_m6_active"] is True
    assert summary["diagnosis"]["first_broken_stage"] == "L4"
