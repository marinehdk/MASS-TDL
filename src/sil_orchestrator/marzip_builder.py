"""MCAP → polars → Arrow.ipc post-processing + Marzip 7-piece assembly.

Ref: Doc 2 §2.4 / §11, Doc 4 §10 Evidence Pack.
"""

from __future__ import annotations
import hashlib
import json
import shutil
import tempfile
import time
from pathlib import Path
from typing import Optional

import polars as pl

from sil_orchestrator.marzip_schemas import (
    SCORING_COLUMNS, VERDICT_SCHEMA, ASDR_EVENT_FIELDS,
    THRESHOLD_PASS, THRESHOLD_RISK, THRESHOLD_SAFETY_MIN,
)

# ---------------------------------------------------------------------------
# MCAP → polars DataFrame
# ---------------------------------------------------------------------------
def mcap_to_polars(mcap_path: str) -> pl.DataFrame:
    """Read rosbag2 MCAP file and extract scoring-relevant columns into polars DataFrame.

    Falls back to empty DataFrame with correct schema if bag is empty, missing,
    or rosbags library is not installed.
    """
    mcap_file = Path(mcap_path)
    if not mcap_file.exists() or mcap_file.stat().st_size == 0:
        return _empty_scoring_df()

    try:
        from rosbags.highlevel import AnyReader
        from rosbags.typesys import Stores, get_typestore

        typestore = get_typestore(Stores.ROS2_HUMBLE)

        rows = []
        with AnyReader([mcap_file]) as reader:
            connections = [c for c in reader.connections
                          if c.topic in ("/sil/scoring", "/sil/own_ship_state",
                                         "/sil/asdr_event")]
            for conn, timestamp, rawdata in reader.messages(connections=connections):
                msg = typestore.deserialize_cdr(rawdata, conn.msgtype)
                if conn.topic == "/sil/scoring":
                    rows.append({
                        "stamp_ns": timestamp,
                        "safety": getattr(msg, "safety", 0.0),
                        "rule_compliance": getattr(msg, "rule_compliance", 0.0),
                        "delay": getattr(msg, "delay", 0.0),
                        "magnitude": getattr(msg, "magnitude", 0.0),
                        "phase": getattr(msg, "phase", 0.0),
                        "plausibility": getattr(msg, "plausibility", 0.0),
                        "total_score": getattr(msg, "total_score", 0.0),
                        "cpa_m": getattr(msg, "cpa_m", 0.0),
                        "tcpa_s": getattr(msg, "tcpa_s", 0.0),
                        "grounding_risk": getattr(msg, "grounding_risk", 0.0),
                        "route_deviation_m": getattr(msg, "route_deviation_m", 0.0),
                        "encounter_type": getattr(msg, "encounter_type", "NONE"),
                        "active_rule": getattr(msg, "active_rule", ""),
                    })
        return pl.DataFrame(rows) if rows else _empty_scoring_df()

    except ImportError:
        return _empty_scoring_df()


def _empty_scoring_df() -> pl.DataFrame:
    """Return empty DataFrame with correct scoring schema."""
    float_cols = [c for c in SCORING_COLUMNS
                  if c not in ("stamp_ns", "target_mmsi", "encounter_type", "active_rule")]
    return pl.DataFrame(
        {col: pl.Series([], dtype=pl.Float64) for col in float_cols},
        schema_overrides={
            "stamp_ns": pl.UInt64,
            "target_mmsi": pl.Int32,
            "encounter_type": pl.Utf8,
            "active_rule": pl.Utf8,
        },
    )


# ---------------------------------------------------------------------------
# Derived KPIs
# ---------------------------------------------------------------------------
def derive_kpis(df: pl.DataFrame) -> dict:
    """Compute derived KPIs from scoring DataFrame."""
    kpis = {}

    if len(df) == 0:
        return {
            "min_cpa_nm": 0.0, "avg_rot_dpm": 0.0, "distance_nm": 0.0,
            "duration_s": 0.0, "max_rudder_deg": 0.0,
            "grounding_risk_max": 0.0, "route_deviation_max_m": 0.0,
            "time_to_mrm_s": 0.0, "decision_count": 0,
        }

    # min CPA (m → NM)
    if "cpa_m" in df.columns and len(df) > 0:
        kpis["min_cpa_nm"] = df["cpa_m"].min() * 0.000539957

    # avg ROT (rad/s → deg/min)
    if "own_rot" in df.columns and len(df) > 0:
        kpis["avg_rot_dpm"] = df["own_rot"].abs().mean() * (180.0 / 3.1415926535)

    # cumulative distance (NM) from own-ship position delta
    if "own_lat" in df.columns and "own_lon" in df.columns and len(df) > 1:
        lat_delta = df["own_lat"].diff().fill_null(0)
        lon_delta = df["own_lon"].diff().fill_null(0)
        avg_lat = df["own_lat"].mean()
        avg_lat_rad = avg_lat * 3.1415926535 / 180.0
        dy = lat_delta * 111120.0
        dx = lon_delta * 111120.0 * abs(avg_lat_rad)
        dist_m = (dy.pow(2) + dx.pow(2)).sqrt()
        kpis["distance_nm"] = dist_m.sum() * 0.000539957

    # duration (ns → s)
    if "stamp_ns" in df.columns and len(df) > 0:
        kpis["duration_s"] = (df["stamp_ns"].max() - df["stamp_ns"].min()) / 1e9

    # max rudder angle (rad → deg)
    if "own_rudder_angle" in df.columns and len(df) > 0:
        kpis["max_rudder_deg"] = df["own_rudder_angle"].abs().max() * (180.0 / 3.1415926535)

    if "grounding_risk" in df.columns and len(df) > 0:
        kpis["grounding_risk_max"] = df["grounding_risk"].max()

    if "route_deviation_m" in df.columns and len(df) > 0:
        kpis["route_deviation_max_m"] = df["route_deviation_m"].max()

    if "active_rule" in df.columns and len(df) > 0:
        kpis["decision_count"] = df["active_rule"].n_unique()

    kpis.setdefault("time_to_mrm_s", 0.0)

    return kpis


# ---------------------------------------------------------------------------
# Verdict builder
# ---------------------------------------------------------------------------
def build_verdict(df: pl.DataFrame, kpis: dict,
                  fault_flags: dict | None = None) -> dict:
    """Build verdict.json from scoring DataFrame + KPIs."""
    ff = fault_flags or {"fatal": [], "warning": [], "info": []}

    if len(df) == 0:
        return {
            "verdict": "INCONCLUSIVE",
            "total_score": 0.0,
            "dimension_scores": {d: 0.0 for d in
                ["safety", "rule_compliance", "delay", "magnitude", "phase", "plausibility"]},
            "kpis": kpis,
            "rule_chain": [],
            "fault_flags": ff,
        }

    total = df["total_score"].mean() if "total_score" in df.columns else 0.0
    safety = df["safety"].min() if "safety" in df.columns else 0.0

    if ff.get("fatal") or total < THRESHOLD_RISK:
        verdict = "FAIL"
    elif safety < THRESHOLD_SAFETY_MIN:
        verdict = "RISK"
    elif total >= THRESHOLD_PASS:
        verdict = "PASS"
    else:
        verdict = "RISK"

    # Rule chain extraction
    rules = []
    if "active_rule" in df.columns and "stamp_ns" in df.columns and len(df) > 0:
        t0 = df["stamp_ns"].min()
        seen = set()
        for row in df.iter_rows(named=True):
            rule = row.get("active_rule", "")
            if rule and rule not in seen:
                seen.add(rule)
                rules.append({
                    "rule_ref": rule,
                    "triggered_at_s": (row["stamp_ns"] - t0) / 1e9 if t0 else 0.0,
                    "decision_id": f"d-{len(seen):03d}",
                })

    dims = {}
    for dim in ["safety", "rule_compliance", "delay", "magnitude", "phase", "plausibility"]:
        dims[dim] = round(df[dim].mean(), 4) if dim in df.columns and len(df) > 0 else 0.0

    return {
        "verdict": verdict,
        "total_score": round(total, 4),
        "dimension_scores": dims,
        "kpis": {k: round(v, 4) if isinstance(v, float) else v for k, v in kpis.items()},
        "rule_chain": rules,
        "fault_flags": ff,
    }


# ---------------------------------------------------------------------------
# Marzip assembly (7-piece container)
# ---------------------------------------------------------------------------
def assemble_marzip(run_id: str, run_dir_parent: Path, export_dir: Path) -> str:
    """Assemble complete 7-piece Marzip evidence container.

    Returns path to .marzip file.

    7 artifacts (+ checksum = 8 files):
      1. scenario.yaml       — original scenario definition
      2. scenario.sha256     — SHA-256 checksum of scenario.yaml
      3. manifest.yaml       — toolchain + format metadata
      4. scoring.arrow       — polars DataFrame as Arrow IPC
      5. results.bag.mcap    — raw rosbag2 recording (if exists)
      6. results.bag.mcap.sha256 — checksum of MCAP
      7. asdr_events.jsonl   — human-readable ASDR events
      8. verdict.json        — final assessment
    """
    run_path = run_dir_parent / run_id
    export_path = export_dir / f"{run_id}_evidence.marzip"
    export_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)

        # 1. Copy scenario artifacts
        for fname in ("scenario.yaml", "scenario.sha256"):
            src = run_path / fname
            if src.exists():
                shutil.copy(src, tmp_dir / fname)

        # 2. Write manifest
        manifest = {
            "run_id": run_id,
            "toolchain": "sil_orchestrator v0.2.0",
            "format": "marzip-1.0",
            "assembled_at": time.time(),
        }
        (tmp_dir / "manifest.yaml").write_text(
            json.dumps(manifest, indent=2, default=str))

        # 3. MCAP → Arrow post-processing
        bag_path = run_path / "results.bag.mcap"
        scoring_arrow_path = run_path / "scoring.arrow"
        if bag_path.exists():
            scoring_df = mcap_to_polars(str(bag_path))
            scoring_df.write_ipc(str(scoring_arrow_path))

            # Copy MCAP + checksum
            shutil.copy(bag_path, tmp_dir / "results.bag.mcap")
            mcap_hash = hashlib.sha256(bag_path.read_bytes()).hexdigest()
            (tmp_dir / "results.bag.mcap.sha256").write_text(mcap_hash + "\n")
        else:
            scoring_df = _empty_scoring_df()
            scoring_df.write_ipc(str(scoring_arrow_path))

        # Copy scoring.arrow into marzip
        if scoring_arrow_path.exists():
            shutil.copy(scoring_arrow_path, tmp_dir / "scoring.arrow")

        # 4. Build asdr_events.jsonl
        asdr_path = run_path / "asdr_events.jsonl"
        if asdr_path.exists():
            shutil.copy(asdr_path, tmp_dir / "asdr_events.jsonl")
        else:
            (tmp_dir / "asdr_events.jsonl").write_text("")

        # 5. Build verdict.json
        kpis = derive_kpis(scoring_df)
        verdict = build_verdict(scoring_df, kpis)
        verdict["run_id"] = run_id

        scenario_yaml_path = run_path / "scenario.yaml"
        if scenario_yaml_path.exists():
            import yaml
            try:
                with open(scenario_yaml_path) as fh:
                    sc = yaml.safe_load(fh) or {}
                verdict["scenario_id"] = sc.get("id", run_id)
            except Exception:
                verdict["scenario_id"] = run_id
        else:
            verdict["scenario_id"] = run_id
        verdict["started_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        verdict["completed_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

        (tmp_dir / "verdict.json").write_text(json.dumps(verdict, indent=2))

        # 6. Zip → .marzip
        zip_base = str(export_path.with_suffix(""))
        shutil.make_archive(zip_base, "zip", tmp_dir)
        zip_path = Path(zip_base + ".zip")
        if zip_path.exists():
            zip_path.rename(export_path)

    return str(export_path)
