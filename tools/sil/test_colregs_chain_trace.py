from tools.sil.colregs_chain_trace import build_chain_summary, attach_gate_diagnosis


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


def test_chain_summary_shape_is_json_report_safe():
    summary = build_chain_summary([
        rec(1.0, "/l3/m5/avoidance_plan", status="NORMAL", waypoints=[]),
    ])
    assert set(summary) == {"route", "m2", "m6", "m4", "m5", "lifecycle", "l4", "m7", "diagnosis"}
    assert isinstance(summary["diagnosis"]["first_broken_stage"], str)
    assert isinstance(summary["diagnosis"]["reason"], str)


def test_solver_status_is_evidence_not_m5_health_flip():
    records = [
        rec(1.0, "/l3/m5/avoidance_plan", solver_status="EMPTY", n_waypoints=0),
        rec(2.0, "/l3/m5/avoidance_plan", solver_status="VALID", n_waypoints=2),
        rec(3.0, "/l3/m5/avoidance_plan", solver_status="EMPTY", n_waypoints=0),
    ]
    summary = build_chain_summary(records)
    assert summary["m5"]["solver_status_transitions"] == ["EMPTY->VALID", "VALID->EMPTY"]
    assert summary["m5"]["status_transitions"] == []
    assert summary["m5"]["valid_plan_samples"] == 1
    assert summary["diagnosis"]["first_broken_stage"] == "OK"


def test_m5_enriched_rationale_does_not_change_status_transition_logic():
    records = [
        rec(
            1.0,
            "/l3/m5/avoidance_plan",
            status="DEGRADED",
            rationale="planner_health=GEOMETRIC_FALLBACK; semantic_mode=AVOIDANCE; fallback_reason=solver_failed",
            waypoints=[{"turn_radius_m": 200.0}],
        ),
        rec(
            2.0,
            "/l3/m5/avoidance_plan",
            status="RECOVERY",
            rationale="planner_health=RECOVERY; semantic_mode=RECOVERY; fallback_reason=none",
            waypoints=[{"turn_radius_m": 300.0}],
        ),
    ]
    summary = build_chain_summary(records)
    assert summary["m5"]["status_transitions"] == ["DEGRADED->RECOVERY"]
    assert summary["m5"]["valid_plan_samples"] == 2


def test_l4_execution_sources_are_parsed_from_asdr_records():
    summary = build_chain_summary([
        rec(
            1.0,
            "/l3/asdr/record",
            source_module="L4_Guidance_Adapter",
            decision_type="guidance_cmd",
            decision_json='{"execution_source":"avoidance","rudder_deg":10.0}',
        ),
        rec(
            2.0,
            "/l3/asdr/record",
            source_module="L4_Guidance_Adapter",
            decision_type="guidance_cmd",
            decision_json='{"execution_source":"transit","rudder_deg":0.0}',
        ),
    ])

    assert summary["l4"]["execution_sources"] == ["avoidance", "transit"]
    assert summary["l4"]["execution_source_transitions"] == ["avoidance->transit"]


def test_gnc_execution_status_is_counted_as_l4_handoff_evidence():
    summary = build_chain_summary([
        rec(
            1.0,
            "/l3/gnc/execution_status",
            execution_state="ACCEPTED",
            reason="feasible",
            suggested_action="none",
            plan_id="m5-colregs-1",
            cross_track_error_m=0.5,
        ),
        rec(
            2.0,
            "/l3/gnc/execution_status",
            execution_state="DEFERRED",
            reason="avoidance_active",
            suggested_action="continue_avoidance",
            plan_id="m5-colregs-1",
            cross_track_error_m=0.8,
        ),
    ])

    assert summary["l4"]["samples"] == 2
    assert summary["l4"]["gnc_execution_state_counts"] == {
        "ACCEPTED": 1,
        "DEFERRED": 1,
    }
    assert summary["l4"]["gnc_reason_counts"] == {
        "avoidance_active": 1,
        "feasible": 1,
    }
    assert summary["l4"]["gnc_plan_id_changes"] == 0


def test_gate_diagnosis_maps_cpa_shortfall_to_m5_contract():
    summary = build_chain_summary([
        rec(1.0, "/l3/m6/colregs_constraint", conflict_detected=True),
        rec(1.0, "/l3/m4/behavior_plan", behavior=1),
        rec(
            1.0,
            "/l3/m5/avoidance_plan",
            planner_health="GEOMETRIC_FALLBACK",
            n_waypoints=3,
        ),
        rec(
            1.0,
            "/l3/asdr/record",
            source_module="L4_Guidance_Adapter",
            decision_type="guidance_cmd",
            decision_json='{"execution_source":"avoidance"}',
        ),
    ])

    diagnosed = attach_gate_diagnosis(summary, {
        "overall_pass": False,
        "cpa_ok": False,
        "min_cpa_m": 875.7,
        "cpa_floor_m": 900.0,
        "domain_gates": {"risk_gate_ok": True, "seamanship_gate_ok": True},
        "route_return_required": True,
        "returned_to_route": True,
        "overtake_required": False,
        "phase_semantics": {"phase_semantics_ok": True},
        "stability_pass": True,
    })

    assert diagnosed["diagnosis"]["first_broken_stage"] == "M5"
    assert diagnosed["diagnosis"]["failing_gate"] == "CPA"
    assert "CPA floor" in diagnosed["diagnosis"]["reason"]


def test_gate_diagnosis_maps_avoidance_latch_to_m4_release():
    summary = build_chain_summary([
        rec(1.0, "/l3/m6/colregs_constraint", conflict_detected=True),
        rec(1.0, "/l3/m4/behavior_plan", behavior=1),
        rec(2.0, "/l3/m4/behavior_plan", behavior=1),
        rec(
            1.0,
            "/l3/m5/avoidance_plan",
            planner_health="GEOMETRIC_FALLBACK",
            n_waypoints=3,
        ),
    ])

    diagnosed = attach_gate_diagnosis(summary, {
        "overall_pass": False,
        "cpa_ok": True,
        "domain_gates": {"risk_gate_ok": True, "seamanship_gate_ok": False},
        "route_return_required": True,
        "returned_to_route": False,
        "overtake_required": False,
        "phase_semantics": {"phase_semantics_ok": True},
        "bp_transitions": [[25.3, 0], [123.3, 1]],
        "stability_pass": True,
    })

    assert diagnosed["diagnosis"]["first_broken_stage"] == "M4"
    assert diagnosed["diagnosis"]["failing_gate"] == "ROUTE_RETURN"
    assert "no recovery/transit release" in diagnosed["diagnosis"]["reason"]


def test_gate_diagnosis_maps_risk_gate_to_m7():
    summary = build_chain_summary([
        rec(1.0, "/l3/m6/colregs_constraint", conflict_detected=True),
        rec(1.0, "/l3/m4/behavior_plan", behavior=1),
        rec(1.0, "/l3/m5/avoidance_plan", planner_health="GEOMETRIC_FALLBACK", n_waypoints=3),
    ])

    diagnosed = attach_gate_diagnosis(summary, {
        "overall_pass": False,
        "cpa_ok": True,
        "domain_gates": {"risk_gate_ok": False, "seamanship_gate_ok": True},
        "route_return_required": True,
        "returned_to_route": True,
        "overtake_required": False,
        "phase_semantics": {"phase_semantics_ok": True},
        "stability_pass": True,
    })

    assert diagnosed["diagnosis"]["first_broken_stage"] == "M7"
    assert diagnosed["diagnosis"]["failing_gate"] == "RISK"


def test_m6_encounter_lifecycle_transition_sequence_is_captured():
    """Phase 1.2 (G-M6-1): chain summary must surface the M6 encounter_state
    sequence so a stuck-at-ONSET bug (R8) is visible directly from trace,
    without geometry back-projection.
    """
    records = [
        rec(1.0, "/l3/m6/colregs_constraint", conflict_detected=False, encounter_state=0),  # CLEAR
        rec(2.0, "/l3/m6/colregs_constraint", conflict_detected=True,  encounter_state=1),  # ONSET
        rec(3.0, "/l3/m6/colregs_constraint", conflict_detected=True,  encounter_state=2),  # ACTIVE
        rec(4.0, "/l3/m6/colregs_constraint", conflict_detected=True,  encounter_state=2),  # ACTIVE (stable)
        rec(5.0, "/l3/m6/colregs_constraint", conflict_detected=False, encounter_state=3, past_clear=True),  # RELEASE
    ]

    summary = build_chain_summary(records)

    assert summary["m6"]["encounter_state_first"] == "CLEAR"
    assert summary["m6"]["encounter_state_last"] == "RELEASE"
    # 4 distinct transitions: CLEAR->ONSET, ONSET->ACTIVE, ACTIVE->RELEASE.
    # (ACTIVE->ACTIVE within-state does not count.)
    assert summary["m6"]["encounter_state_transitions"] == [
        "CLEAR->ONSET",
        "ONSET->ACTIVE",
        "ACTIVE->RELEASE",
    ]
    assert summary["m6"]["past_clear_samples"] == 1
    assert summary["m6"]["samples"] == 5


def test_m6_encounter_lifecycle_stuck_at_onset_is_visible():
    """Phase 1.2 regression guard for R8: when M6 stays at ONSET the entire
    encounter (the bug fixed by Phase 1.1 rank-based aggregation), the chain
    summary must show first==last=="ONSET" and an empty transition list so
    the diagnostic is unambiguous.
    """
    records = [
        rec(1.0, "/l3/m6/colregs_constraint", conflict_detected=True, encounter_state=1),
        rec(2.0, "/l3/m6/colregs_constraint", conflict_detected=True, encounter_state=1),
        rec(3.0, "/l3/m6/colregs_constraint", conflict_detected=True, encounter_state=1),
    ]

    summary = build_chain_summary(records)

    assert summary["m6"]["encounter_state_first"] == "ONSET"
    assert summary["m6"]["encounter_state_last"] == "ONSET"
    assert summary["m6"]["encounter_state_transitions"] == []
    assert summary["m6"]["past_clear_samples"] == 0


def test_m6_encounter_lifecycle_absent_fields_default_safely():
    """Older traces (pre-Phase 1.2) lack encounter_state / past_clear fields.
    The chain summary must not crash and must report None / 0 defaults so the
    tool stays usable on legacy evidence.
    """
    records = [
        rec(1.0, "/l3/m6/colregs_constraint", conflict_detected=True),  # no encounter_state
        rec(2.0, "/l3/m6/colregs_constraint", conflict_detected=True),
    ]

    summary = build_chain_summary(records)

    # encounter_state defaults to 0 (CLEAR) when absent; first/last reflect that.
    assert summary["m6"]["encounter_state_first"] == "CLEAR"
    assert summary["m6"]["encounter_state_last"] == "CLEAR"
    assert summary["m6"]["encounter_state_transitions"] == []
    assert summary["m6"]["past_clear_samples"] == 0
