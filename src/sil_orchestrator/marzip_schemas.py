"""Marzip container schemas: polars scoring DataFrame, verdict.json, asdr_events.jsonl.

Ref: Doc 2 §2.4 / §11, Doc 4 §10 Evidence Pack.
"""

from __future__ import annotations

# ---------------------------------------------------------------------------
# 1. Scoring DataFrame schema (Arrow .ipc)
# ---------------------------------------------------------------------------
SCORING_SCHEMA: dict[str, type] = {
    # Time column (from MCAP message timestamp, nanoseconds since epoch)
    "stamp_ns": int,            # UInt64 in polars
    # 6-dim Hagen 2022 scoring (range 0.0–1.0, higher = better)
    "safety": float,
    "rule_compliance": float,
    "delay": float,
    "magnitude": float,
    "phase": float,
    "plausibility": float,
    # Total weighted score
    "total_score": float,
    # CPA metrics (computed from own_ship + target geometry)
    "cpa_m": float,             # Closest Point of Approach (meters)
    "tcpa_s": float,            # Time to CPA (seconds)
    # Own-ship state snapshot
    "own_lat": float,
    "own_lon": float,
    "own_sog": float,           # Speed over ground (m/s)
    "own_cog": float,           # Course over ground (rad)
    "own_rot": float,           # Rate of turn (rad/s)
    "own_rudder_angle": float,
    "own_throttle": float,
    # Target vessel state (nearest)
    "target_mmsi": int,
    "target_lat": float,
    "target_lon": float,
    "target_sog": float,
    "target_cog": float,
    "target_range_m": float,
    "target_bearing_rad": float,
    # COLREGs classification at this timestep
    "encounter_type": str,      # "HO" | "CR" | "OT" | "MS" | "NONE"
    "active_rule": str,         # e.g. "Rule 14", "Rule 15"
    # Safety checks
    "grounding_risk": float,    # 0.0–1.0, from depth+bathymetry
    "route_deviation_m": float, # lateral deviation from planned route
}

SCORING_COLUMNS: list[str] = list(SCORING_SCHEMA.keys())

# ---------------------------------------------------------------------------
# 2. Derived KPI summary (for verdict.json kpis section)
# ---------------------------------------------------------------------------
KPI_DERIVATION: dict[str, dict] = {
    "min_cpa_nm": {
        "source": "cpa_m",
        "description": "Minimum CPA across entire run, converted meters→NM",
    },
    "avg_rot_dpm": {
        "source": "own_rot",
        "description": "Absolute mean rate of turn, rad/s → deg/min",
    },
    "distance_nm": {
        "source": None,  # Computed from cumulative own-ship position delta
        "description": "Total distance traveled, meters→NM",
    },
    "duration_s": {
        "source": "stamp_ns",
        "description": "Wall-clock duration, nanoseconds→seconds",
    },
    "max_rudder_deg": {
        "source": "own_rudder_angle",
        "description": "Maximum absolute rudder angle, rad→deg",
    },
    "grounding_risk_max": {
        "source": "grounding_risk",
        "description": "Maximum grounding risk score",
    },
    "route_deviation_max_m": {
        "source": "route_deviation_m",
        "description": "Maximum lateral deviation from planned route (m)",
    },
    "time_to_mrm_s": {
        "source": None,  # Time from first maneuver recommendation (MRM flag in ASDR)
        "description": "Seconds to first maneuver recommendation",
    },
    "decision_count": {
        "source": None,  # Count of distinct decision_id in ASDR events
        "description": "Number of distinct COLREGs decisions",
    },
}

# ---------------------------------------------------------------------------
# 3. verdict.json schema
# ---------------------------------------------------------------------------
VERDICT_SCHEMA: dict = {
    "type": "object",
    "required": [
        "run_id", "scenario_id", "started_at", "completed_at",
        "verdict", "total_score", "dimension_scores", "kpis",
        "rule_chain", "fault_flags",
    ],
    "properties": {
        "run_id": {"type": "string"},
        "scenario_id": {"type": "string"},
        "started_at": {"type": "string", "format": "date-time"},
        "completed_at": {"type": "string", "format": "date-time"},
        "verdict": {
            "type": "string",
            "enum": ["PASS", "RISK", "FAIL", "INCONCLUSIVE"],
            "description": (
                "PASS: total_score >= 0.70 AND safety > 0.80 AND no fatal faults. "
                "RISK: total_score >= 0.50 OR has warning faults. "
                "FAIL: total_score < 0.50 OR any fatal faults."
            ),
        },
        "total_score": {"type": "number", "minimum": 0.0, "maximum": 1.0},
        "dimension_scores": {
            "type": "object",
            "properties": {
                "safety": {"type": "number"},
                "rule_compliance": {"type": "number"},
                "delay": {"type": "number"},
                "magnitude": {"type": "number"},
                "phase": {"type": "number"},
                "plausibility": {"type": "number"},
            },
        },
        "kpis": {
            "type": "object",
            "properties": {
                "min_cpa_nm": {"type": "number"},
                "avg_rot_dpm": {"type": "number"},
                "distance_nm": {"type": "number"},
                "duration_s": {"type": "number"},
                "max_rudder_deg": {"type": "number"},
                "grounding_risk_max": {"type": "number"},
                "route_deviation_max_m": {"type": "number"},
                "time_to_mrm_s": {"type": "number"},
                "decision_count": {"type": "integer"},
            },
        },
        "rule_chain": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "rule_ref": {"type": "string"},
                    "triggered_at_s": {"type": "number"},
                    "decision_id": {"type": "string"},
                },
            },
        },
        "fault_flags": {
            "type": "object",
            "properties": {
                "fatal": {"type": "array", "items": {"type": "string"}},
                "warning": {"type": "array", "items": {"type": "string"}},
                "info": {"type": "array", "items": {"type": "string"}},
            },
        },
    },
}

# ---------------------------------------------------------------------------
# 4. asdr_events.jsonl schema (human-readable, one JSON object per line)
# ---------------------------------------------------------------------------
ASDR_EVENT_FIELDS: list[str] = [
    "timestamp",        # ISO 8601 with nanoseconds
    "event_type",       # e.g. "COLREGS_RULE_14", "M4_BEHAVIOR_SWITCH", "M7_VETO"
    "rule_ref",         # COLREGs rule reference
    "decision_id",      # Unique decision identifier
    "verdict",          # "PASS" | "RISK" | "FAIL"
    "confidence",       # 0.0–1.0
    "rationale",        # Human-readable explanation
    "own_lat",
    "own_lon",
    "target_mmsi",
    "target_range_m",
    "target_bearing_rad",
    "payload_json",     # Additional structured data (JSON string)
]

# ---------------------------------------------------------------------------
# 5. Scoring thresholds
# ---------------------------------------------------------------------------
THRESHOLD_PASS: float = 0.70
THRESHOLD_RISK: float = 0.50
THRESHOLD_SAFETY_MIN: float = 0.80
