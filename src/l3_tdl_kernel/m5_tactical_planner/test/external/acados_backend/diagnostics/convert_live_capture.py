#!/usr/bin/env python3
"""Convert canonical dispatcher-boundary MidMpcInput capture to replay schema."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    raw = args.capture.read_bytes()
    captured = json.loads(raw)
    source = captured["mid_mpc_input"]
    c = source["constraints"]
    direction = int(source["colregs_preferred_direction"])
    case = {
        "case_id": "rule14_ho_live_dispatch_749728000002",
        "provenance": {"source": str(args.capture), "boundary": captured["boundary"],
                       "schema": captured["schema"], "source_sha256": hashlib.sha256(raw).hexdigest(),
                       "stamp_ns": source["stamp_ns"],
                       "note": "Exact default-off dispatcher solve-entry capture; replay projection preserves every solver-consumed field."},
        "own_ship": source["own_ship"],
        "route": {"bearing_rad": source["planned_route_bearing_rad"],
                  "planned_speed_mps": source["planned_speed_mps"],
                  "frame_origin_x_m": source["route_frame_origin_x_m"],
                  "frame_origin_y_m": source["route_frame_origin_y_m"],
                  "frame_normal_x": source["route_frame_normal_x"],
                  "frame_normal_y": source["route_frame_normal_y"],
                  "active_leg_bearing_rad": source["route_frame_active_leg_bearing_rad"],
                  "lateral_scale_m": source["lateral_scale_m"], "route_weight": source["route_weight"]},
        "constraints": {"heading_min_rad": c["heading_min_rad"], "heading_max_rad": c["heading_max_rad"],
                        "speed_min_mps": c["speed_min_mps"], "speed_max_mps": c["speed_max_mps"],
                        "cpa_safe_m": c["cpa_safe_m"], "cpa_hard_m": c["cpa_hard_m"],
                        "own_ship_psi_rad": c["own_ship_psi_rad"],
                        "rot_max_rad_s": source["rot_max_rad_s"], "decel_max_mps2": source["decel_max_mps2"],
                        "applicable_rules": c["applicable_rules"],
                        "earliest_min_alt_k": c["earliest_min_alt_k"],
                        "min_alt_required_rad": c["min_alt_required_rad"],
                        "heading_box_reachable_from_psi0_deg": c["heading_box_reachable_from_psi0_deg"],
                        "rot_step_deg": c["rot_step_deg"]},
        "colregs": {"conflict_active": source["colregs_conflict_active"],
                    "primary_role": source["colregs_primary_role"],
                    "preferred_direction": 1 if direction == 1 else -1 if direction == 2 else 0,
                    "preferred_direction_enum": direction,
                    "min_alteration_rad": source["colregs_min_alteration_rad"],
                    "encounter_state_enum": source["colregs_encounter_state"],
                    "has_m6_encounter_state": source["has_m6_encounter_state"], "phase": source["colregs_phase"]},
        "prefix": {"active_k": source["prefix_active_k"], "psi_rad": source["prefix_psi_rad"],
                   "u_mps": source["prefix_u_mps"]},
        "targets": source["targets"],
        "source_mid_mpc_input": source,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(case, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"case_id": case["case_id"], "source_sha256": case["provenance"]["source_sha256"],
                      "output_sha256": hashlib.sha256(args.output.read_bytes()).hexdigest()}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
