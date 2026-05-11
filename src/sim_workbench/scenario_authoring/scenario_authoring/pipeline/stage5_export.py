"""Stage 5: maritime-schema TrafficSituation YAML export with metadata extensions."""
from __future__ import annotations

import hashlib
from datetime import UTC, datetime
from pathlib import Path

import yaml

from scenario_authoring.authoring.ais_source import ne_to_latlon
from scenario_authoring.pipeline.stage4_filter import DCPA_THRESHOLD_M, Encounter

OUTPUT_DIR = Path("scenarios/ais_derived")


def _ts_to_iso(ts: float) -> str:
    return datetime.fromtimestamp(ts, tz=UTC).isoformat()


def _encounter_type_to_rule(enc_type: str) -> str:
    return {"HEAD_ON": "Rule14", "CROSSING": "Rule15",
            "OVERTAKING": "Rule13"}.get(enc_type, "Unknown")


def export_encounter_to_yaml(
    encounter: Encounter,
    geo_origin: tuple[float, float],
    output_dir: Path = OUTPUT_DIR,
    raw_csv_path: Path | None = None,
) -> Path:
    """Export a single encounter as maritime-schema TrafficSituation YAML.

    Returns path to written YAML file.
    """
    os_traj = encounter.own_trajectory
    ts_traj = encounter.target_trajectory

    # Trajectory columns: 0=t, 1=lat, 2=lon, 3=sog, 4=cog
    os_lat0, os_lon0 = ne_to_latlon(geo_origin[0], geo_origin[1],
                                     float(os_traj[0, 1]), float(os_traj[0, 2]))
    ts_lat0, ts_lon0 = ne_to_latlon(geo_origin[0], geo_origin[1],
                                     float(ts_traj[0, 1]), float(ts_traj[0, 2]))

    scenario_id = (
        f"ais-{encounter.target_mmsi}-"
        f"{_ts_to_iso(encounter.t_encounter_start)[:19].replace(':', '')}-"
        f"{encounter.encounter_type.lower()}-v1.0"
    )

    # Traceability hash
    ais_source_hash = ""
    if raw_csv_path and raw_csv_path.exists():
        content = raw_csv_path.read_bytes()
        ais_source_hash = hashlib.sha256(content).hexdigest()[:16]

    duration = float(encounter.t_encounter_end - encounter.t_encounter_start)

    doc = {
        "title": f"AIS-derived {encounter.encounter_type} encounter -- MMSI {encounter.target_mmsi}",
        "description": (
            f"Extracted from AIS data. "
            f"min DCPA={encounter.min_dcpa_m:.0f}m, "
            f"min TCPA={encounter.min_tcpa_s:.0f}s"
        ),
        "start_time": _ts_to_iso(encounter.t_encounter_start),
        "own_ship": {
            "id": "os",
            "nav_status": 0,
            "mmsi": encounter.own_mmsi,
            "initial": {
                "position": {"latitude": os_lat0, "longitude": os_lon0},
                "cog": float(os_traj[0, 4]),
                "sog": float(os_traj[0, 3]),
                "heading": float(os_traj[0, 4]),
            },
        },
        "target_ships": [{
            "id": f"ts-{encounter.target_mmsi}",
            "nav_status": 0,
            "mmsi": encounter.target_mmsi,
            "initial": {
                "position": {"latitude": ts_lat0, "longitude": ts_lon0},
                "cog": float(ts_traj[0, 4]),
                "sog": float(ts_traj[0, 3]),
                "heading": float(ts_traj[0, 4]),
            },
        }],
        "metadata": {
            "schema_version": "2.0",
            "scenario_id": scenario_id,
            "scenario_source": "ais_derived",
            "ais_source_hash": ais_source_hash,
            "ais_mmsi": [encounter.own_mmsi, encounter.target_mmsi],
            "ais_time_window": {
                "start": _ts_to_iso(encounter.t_encounter_start),
                "end": _ts_to_iso(encounter.t_encounter_end),
            },
            "vessel_class": "FCB",
            "odd_zone": "A",
            "geo_origin": {
                "latitude": geo_origin[0],
                "longitude": geo_origin[1],
            },
            "encounter": {
                "rule": _encounter_type_to_rule(encounter.encounter_type),
                "give_way_vessel": "unknown",
            },
            "disturbance_model": {
                "wind_kn": 0.0, "wind_dir_nav_deg": 0.0,
                "current_kn": 0.0, "current_dir_nav_deg": 0.0,
                "vis_m": 10000.0, "wave_height_m": 0.0,
            },
            "pass_criteria": {
                "max_dcpa_no_action_m": DCPA_THRESHOLD_M,
                "min_dcpa_with_action_m": DCPA_THRESHOLD_M,
            },
            "simulation": {
                "duration_s": duration,
                "dt_s": 0.02,
                "n_rps_initial": 3.5,
            },
            "prng_seed": None,
        },
    }

    output_dir.mkdir(parents=True, exist_ok=True)
    yaml_path = output_dir / f"{scenario_id}.yaml"
    with open(yaml_path, "w") as f:
        yaml.dump(doc, f, default_flow_style=False, sort_keys=False, allow_unicode=True)

    return yaml_path
