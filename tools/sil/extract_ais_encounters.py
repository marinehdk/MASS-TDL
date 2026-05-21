#!/usr/bin/env python3
"""
AIS encounter extraction script.

Modes:
  - Synthetic (default when --input CSV does not exist):
    Generates N maritime-schema YAMLs from predefined synthetic encounters.
  - CSV (when --input CSV exists):
    Reads AIS CSV, finds encounters (DCPA <= 2.0 NM, TCPA in [0, 30 min]),
    classifies by bearing, and generates YAMLs.

Usage:
  python tools/sil/extract_ais_encounters.py --output scenarios/ais_derived/ --max 5
"""

import argparse
import csv
import math
import sys
from pathlib import Path
from typing import Optional

import yaml

NM_TO_M = 1852.0
KN_TO_MPS = 0.514444
R_EARTH = 6371000.0

_SYNTHETIC_ENCOUNTERS = [
    ("Rule14",           63.440, 10.380, 0.0,   10.0,  63.557, 10.380, 180.0, 10.0),
    ("Rule14",           63.440, 10.380, 5.0,   12.0,  63.550, 10.375, 175.0, 9.0),
    ("Rule15_starboard", 63.440, 10.380, 0.0,   10.0,  63.505, 10.460, 270.0, 10.0),
    ("Rule15_port",      63.440, 10.380, 0.0,   10.0,  63.475, 10.250, 90.0,  10.0),
    ("Rule13",           63.440, 10.380, 0.0,   14.0,  63.486, 10.380, 0.0,   7.0),
]

_ENCOUNTER_PARAMS = {
    "Rule14": {
        "expected_own_action": "turn_starboard",
        "avoidance_time_s": 300.0,
        "avoidance_delta_rad": 0.5236,
        "avoidance_duration_s": 90.0,
    },
    "Rule14b": {
        "expected_own_action": "turn_starboard",
        "avoidance_time_s": 280.0,
        "avoidance_delta_rad": 0.5236,
        "avoidance_duration_s": 90.0,
    },
    "Rule15_starboard": {
        "expected_own_action": "turn_starboard",
        "avoidance_time_s": 250.0,
        "avoidance_delta_rad": 0.6109,
        "avoidance_duration_s": 60.0,
    },
    "Rule15_port": {
        "expected_own_action": "turn_port",
        "avoidance_time_s": 200.0,
        "avoidance_delta_rad": 0.3491,
        "avoidance_duration_s": 60.0,
    },
    "Rule13": {
        "expected_own_action": "turn_starboard",
        "avoidance_time_s": 150.0,
        "avoidance_delta_rad": 0.3491,
        "avoidance_duration_s": 90.0,
    },
}


def _dcpa_tcpa(os_lat, os_lon, os_cog, os_sog, ts_lat, ts_lon, ts_cog, ts_sog):
    lat1, lon1 = math.radians(os_lat), math.radians(os_lon)
    lat2, lon2 = math.radians(ts_lat), math.radians(ts_lon)
    cog1, sog1 = math.radians(os_cog), os_sog * KN_TO_MPS
    cog2, sog2 = math.radians(ts_cog), ts_sog * KN_TO_MPS

    dlat = lat2 - lat1
    dlon = lon2 - lon1
    a = math.sin(dlat / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2) ** 2
    dist = R_EARTH * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))

    y = math.sin(dlon) * math.cos(lat2)
    x = math.cos(lat1) * math.sin(lat2) - math.sin(lat1) * math.cos(lat2) * math.cos(dlon)
    bearing_rad = math.atan2(y, x)

    vx1 = sog1 * math.sin(cog1)
    vy1 = sog1 * math.cos(cog1)
    vx2 = sog2 * math.sin(cog2)
    vy2 = sog2 * math.cos(cog2)
    vrx = vx2 - vx1
    vry = vy2 - vy1

    rel_speed = math.hypot(vrx, vry)
    if rel_speed < 1e-9:
        return dist / NM_TO_M, float("inf")

    rel_cog = math.atan2(vrx, vry)
    dcpa_m = dist * abs(math.sin(rel_cog - bearing_rad))
    dcpa_nm = dcpa_m / NM_TO_M

    tcpa_s = -dist * math.cos(rel_cog - bearing_rad) / rel_speed
    tcpa_min = tcpa_s / 60.0

    return dcpa_nm, tcpa_min


def _bearing(os_lat, os_lon, os_cog, ts_lat, ts_lon):
    lat1, lon1 = math.radians(os_lat), math.radians(os_lon)
    lat2, lon2 = math.radians(ts_lat), math.radians(ts_lon)
    y = math.sin(lon2 - lon1) * math.cos(lat2)
    x = math.cos(lat1) * math.sin(lat2) - math.sin(lat1) * math.cos(lat2) * math.cos(lon2 - lon1)
    return (math.degrees(math.atan2(y, x)) + 360) % 360


def _relative_bearing(os_bearing, os_cog):
    return ((os_bearing - os_cog) + 360) % 360


def _classify_rule(rel_bearing, target_cog, os_cog):
    rel_brg = rel_bearing % 360
    if 337.5 <= rel_brg or rel_brg < 22.5:
        heading_diff = abs(((target_cog - os_cog) + 180 + 360) % 360 - 180)
        if heading_diff > 168.75:
            return "Rule14"
        else:
            course_diff = ((target_cog - os_cog) + 360) % 360
            if course_diff < 180:
                return "Rule13"
            return None
    elif 22.5 <= rel_brg < 90.0:
        heading_diff = abs(((target_cog - os_cog) + 180 + 360) % 360 - 180)
        if heading_diff > 168.75:
            return None
        return "Rule15_starboard"
    elif 270.0 <= rel_brg < 337.5:
        heading_diff = abs(((target_cog - os_cog) + 180 + 360) % 360 - 180)
        if heading_diff > 168.75:
            return None
        return "Rule15_port"
    return None


def _rule_params(rule_key, idx):
    if rule_key == "Rule14":
        if idx == 0:
            return _ENCOUNTER_PARAMS["Rule14"]
        return _ENCOUNTER_PARAMS["Rule14b"]
    return _ENCOUNTER_PARAMS.get(rule_key, _ENCOUNTER_PARAMS["Rule14"])


def _rule_label(rule_key):
    label_map = {
        "Rule14": "Rule14",
        "Rule15_starboard": "Rule15_Stbd",
        "Rule15_port": "Rule15_Port",
        "Rule13": "Rule13",
    }
    return label_map.get(rule_key, rule_key)


def _colregs_rules(rule_key):
    rule_map = {
        "Rule14": ["R14", "R8"],
        "Rule15_starboard": ["R15", "R16", "R8"],
        "Rule15_port": ["R15", "R17", "R8"],
        "Rule13": ["R13", "R16", "R8"],
    }
    return rule_map.get(rule_key, ["R14", "R8"])


def _make_scenario_yaml(idx, rule_key, os_dict, ts_dict, dcpa_nm, source):
    rule_label = _rule_label(rule_key)
    params = _rule_params(rule_key, idx)
    scenario_id = f"ais-{rule_key.replace('_', '-').lower()}-{idx + 1:02d}-v1.0"

    desc_rule_short = rule_key.replace("_", " ").replace("starboard", "starboard").replace("port", "port")
    description = f"Extracted from {source}. Rule {rule_label}. DCPA ~ {dcpa_nm:.2f} NM."

    doc = {
        "title": f"AIS-derived {rule_label} encounter {idx + 1:02d}",
        "description": description,
        "startTime": "2026-01-01T00:00:00Z",
        "ownShip": {
            "static": {
                "id": 1,
                "shipType": "Cargo",
                "name": "FCB Own Ship",
                "mmsi": 123456789,
            },
            "initial": {
                "position": {
                    "latitude": os_dict["lat"],
                    "longitude": os_dict["lon"],
                },
                "cog": os_dict["cog"],
                "sog": os_dict["sog"],
                "heading": os_dict["cog"],
                "navStatus": "Under way using engine",
            },
            "model": "fcb_mmg_vessel",
            "controller": "psbmpc_wrapper",
        },
        "targetShips": [
            {
                "id": "ts1",
                "static": {
                    "id": 2,
                    "mmsi": ts_dict["mmsi"],
                },
                "initial": {
                    "position": {
                        "latitude": ts_dict["lat"],
                        "longitude": ts_dict["lon"],
                    },
                    "cog": ts_dict["cog"],
                    "sog": ts_dict["sog"],
                    "heading": ts_dict["cog"],
                },
                "model": "ais_replay_vessel",
            }
        ],
        "environment": {
            "wind": {"dir_deg": 0.0, "speed_mps": 0.0},
            "current": {"dir_deg": 0.0, "speed_mps": 0.0},
            "visibility_nm": 10.0,
        },
        "metadata": {
            "schema_version": "3.0",
            "scenario_id": scenario_id,
            "vessel_class": "FCB",
            "colregs_rules": _colregs_rules(rule_key),
            "odd_cell": {"domain": "open_sea_offshore_wind_farm"},
            "encounter": {
                "rule": rule_label,
                "give_way_vessel": "own",
                "expected_own_action": params["expected_own_action"],
                "avoidance_time_s": params["avoidance_time_s"],
                "avoidance_delta_rad": params["avoidance_delta_rad"],
                "avoidance_duration_s": params["avoidance_duration_s"],
            },
            "scenario_source": source,
            "expected_outcome": {
                "cpa_min_m_ge": 500.0,
            },
            "simulation_settings": {
                "total_time": 700.0,
                "dt": 0.02,
                "n_rps_initial": _estimate_rps(os_dict["sog"]),
                "coordinate_origin": [63.44, 10.38],
                "dynamics_mode": "internal",
                "backend": "ros2",
            },
            "disturbance": {
                "wind": {"dir_deg": 0.0, "speed_mps": 0.0},
                "current": {"dir_deg": 0.0, "speed_mps": 0.0},
            },
        },
    }
    return doc


def _estimate_rps(sog_kn):
    return round(0.3 * sog_kn, 1)


def _synthetic_encounters(encounter_list, output_dir, max_n):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    for i, enc in enumerate(encounter_list[:max_n]):
        rule_key, os_lat, os_lon, os_cog, os_sog, ts_lat, ts_lon, ts_cog, ts_sog = enc

        dcpa_nm, tcpa_min = _dcpa_tcpa(os_lat, os_lon, os_cog, os_sog, ts_lat, ts_lon, ts_cog, ts_sog)

        os_dict = {"lat": os_lat, "lon": os_lon, "cog": os_cog, "sog": os_sog}
        ts_dict = {
            "lat": ts_lat,
            "lon": ts_lon,
            "cog": ts_cog,
            "sog": ts_sog,
            "mmsi": 100000020 + i,
        }

        doc = _make_scenario_yaml(i, rule_key, os_dict, ts_dict, dcpa_nm, "synthetic_ais_equiv")

        rule_label = _rule_label(rule_key)
        slug = rule_label.lower().replace(" ", "-").replace("_", "-")
        filename = f"ais-{slug}-{i + 1:02d}-v1.0.yaml"
        out_path = output_dir / filename
        out_path.write_text(yaml.dump(doc, allow_unicode=True, default_flow_style=False, sort_keys=False))
        print(f"  Wrote: {out_path}")


def _csv_encounters(csv_path, output_dir, max_n):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    encounters = []
    with open(csv_path, newline="") as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    for i in range(len(rows)):
        for j in range(i + 1, len(rows)):
            if len(encounters) >= max_n:
                break
            r1, r2 = rows[i], rows[j]
            if r1["mmsi"] == r2["mmsi"]:
                continue
            os_data = (float(r1["lat"]), float(r1["lon"]), float(r1["cog_deg"]), float(r1["sog_kn"]))
            ts_data = (float(r2["lat"]), float(r2["lon"]), float(r2["cog_deg"]), float(r2["sog_kn"]))
            dcpa_nm, tcpa_min = _dcpa_tcpa(*os_data, *ts_data)
            if dcpa_nm <= 2.0 and 0.0 <= tcpa_min <= 30.0:
                bearing_deg = _bearing(*os_data[:3], *ts_data[:2])
                rel_brg = _relative_bearing(bearing_deg, os_data[2])
                rule_key = _classify_rule(rel_brg, ts_data[2], os_data[2])
                if rule_key:
                    encounters.append((rule_key, dcpa_nm, os_data, ts_data, int(r2["mmsi"])))

        if len(encounters) >= max_n:
            break

    for idx, (rule_key, dcpa_nm, os_data, ts_data, mmsi) in enumerate(encounters):
        os_dict = {"lat": os_data[0], "lon": os_data[1], "cog": os_data[2], "sog": os_data[3]}
        ts_dict = {"lat": ts_data[0], "lon": ts_data[1], "cog": ts_data[2], "sog": ts_data[3], "mmsi": mmsi}
        doc = _make_scenario_yaml(idx, rule_key, os_dict, ts_dict, dcpa_nm, "ais_csv")
        rule_label = _rule_label(rule_key)
        slug = rule_label.lower().replace(" ", "-").replace("_", "-")
        filename = f"ais-{slug}-{idx + 1:02d}-v1.0.yaml"
        out_path = output_dir / filename
        out_path.write_text(yaml.dump(doc, allow_unicode=True, default_flow_style=False, sort_keys=False))
        print(f"  Wrote: {out_path}")


def main():
    parser = argparse.ArgumentParser(description="AIS encounter extraction — synthetic or CSV")
    parser.add_argument("--input", type=Path, default=None, help="Optional AIS CSV file")
    parser.add_argument("--output", type=Path, required=True, help="Output directory for YAML files")
    parser.add_argument("--max", type=int, default=5, help="Max encounters to generate (default: 5)")
    args = parser.parse_args()

    csv_path: Optional[Path] = args.input
    if csv_path is not None and csv_path.exists():
        print(f"CSV mode: reading {csv_path}")
        _csv_encounters(csv_path, args.output, args.max)
    else:
        if csv_path is not None:
            print(f"CSV not found at {csv_path}, falling back to synthetic mode")
        print(f"Synthetic mode: generating {min(args.max, len(_SYNTHETIC_ENCOUNTERS))} encounters")
        _synthetic_encounters(_SYNTHETIC_ENCOUNTERS, args.output, args.max)

    print("Done.")


if __name__ == "__main__":
    main()
