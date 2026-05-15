#!/usr/bin/env python3
"""
migrate_scenario_yaml.py — Schema migration tool for MASS L3 TDL scenario YAML.

Converts v1.0 (ENU-format) and v2.0 (IMAZU-format) YAML scenario files to
the canonical maritime-schema TrafficSituation + FCB metadata extension format.

Usage:
    # Dry-run: report what would change without writing
    python tools/migrate_scenario_yaml.py --dry-run --file scenarios/COLREGs测试/colreg-rule14-ho.yaml

    # Migrate a single file
    python tools/migrate_scenario_yaml.py --file scenarios/COLREGs测试/colreg-rule14-ho.yaml

    # Migrate all v1/v2 files in the scenarios directory tree
    python tools/migrate_scenario_yaml.py --all

    # Rollback a file migration
    python tools/migrate_scenario_yaml.py --rollback scenarios/COLREGs测试/colreg-rule14-ho.yaml

    # Rollback all migrations
    python tools/migrate_scenario_yaml.py --rollback all
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import math
import os
import shutil
import sys
from pathlib import Path

import yaml

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

EARTH_R = 6371000.0  # mean Earth radius (m)
MPS_TO_KN = 1.94384
KN_TO_MPS = 0.514444
M_TO_NM = 1.0 / 1852.0
SCHEMA_VERSION = "3.0"
DEFAULT_ORIGIN = (63.44, 10.38)  # Norwegian Sea anchor

ODD_ZONE_MAP = {
    "A": "open_sea_offshore_wind_farm",
    "B": "coastal_archipelago",
    "C": "harbour_approach",
    "D": "restricted_fairway",
}

# Map from v1 rule_branch_covered token → COLREGs rule code
COLREGS_RULE_TOKEN_MAP: dict[str, str] = {
    "Rule14_HeadOn": "R14",
    "Rule15_StbdCrossing": "R15",
    "Rule15_PortCrossing": "R15",
    "Rule8_Action": "R8",
    "Rule17_StandOn": "R17",
    "Rule5_LookOut": "R5",
    "Rule7_RiskOfCollision": "R7",
    "Rule2_Responsibility": "R2",
    "Rule3_Definitions": "R3",
    "Rule6_SafeSpeed": "R6",
    "Rule9_NarrowChannel": "R9",
    "Rule10_TSS": "R10",
    "Rule13_Overtaking": "R13",
    "Rule16_ActionByGiveWay": "R16",
    "Rule18_Responsibilities": "R18",
    "Rule19_RestrictedVisibility": "R19",
    "Rule34_ManeuverSignals": "R34",
    "Rule35_SoundSignals": "R35",
}

AIS_NAV_STATUS_MAP: dict[int, str] = {
    0: "Under way using engine",
    1: "At anchor",
    2: "Not under command",
    3: "Restricted maneuverability",
    4: "Constrained by her draught",
    5: "Moored",
    6: "Aground",
    7: "Engaged in fishing",
    8: "Under way sailing",
    9: "Power-driven vessel towing astern",
    10: "Power-driven vessel pushing ahead or towing alongside",
    14: "AIS-SART (active)",
    15: "Undefined",
}


# ---------------------------------------------------------------------------
# Geo helpers
# ---------------------------------------------------------------------------

def enu_to_latlon(
    x_m: float,
    y_m: float,
    origin: tuple[float, float] = DEFAULT_ORIGIN,
) -> tuple[float, float]:
    """Convert ENU (x=east, y=north, metres) to (latitude, longitude) WGS84.

    Uses spherical-Earth approximation with R = 6 371 000 m.
    """
    lat0_rad = math.radians(origin[0])
    lon0_rad = math.radians(origin[1])

    dlat_rad = y_m / EARTH_R
    dlon_rad = x_m / (EARTH_R * math.cos(lat0_rad))

    lat = math.degrees(lat0_rad + dlat_rad)
    lon = math.degrees(lon0_rad + dlon_rad)
    return (lat, lon)


# ---------------------------------------------------------------------------
# AIS nav status conversion
# ---------------------------------------------------------------------------

def ais_nav_status_int_to_string(val: int) -> str:
    """Map integer AIS navigational status → maritime-schema enum string."""
    return AIS_NAV_STATUS_MAP.get(val, "Undefined")


# ---------------------------------------------------------------------------
# Schema version detection
# ---------------------------------------------------------------------------

def detect_schema_version(doc: dict) -> str:
    """Return ``'v1'``, ``'v2'``, ``'legacy'``, or ``'unknown'``."""
    sv = doc.get("schema_version")
    if sv == "1.0":
        return "v1"
    # v2 uses metadata.schema_version = "2.0"
    meta = doc.get("metadata", {})
    if isinstance(meta, dict):
        msv = meta.get("schema_version")
        if msv == "2.0":
            return "v2"
    # Heuristic: has initial_conditions + own_ship with x_m → v1
    if "initial_conditions" in doc and isinstance(doc.get("initial_conditions"), dict):
        os_ = doc["initial_conditions"].get("own_ship", {})
        if isinstance(os_, dict) and "x_m" in os_:
            return "v1"
    # Heuristic: has own_ship.id like "os" with metadata → v2
    if "own_ship" in doc and isinstance(doc.get("own_ship"), dict):
        if "initial" in doc["own_ship"] and isinstance(meta, dict) and "scenario_id" in meta:
            return "v2"
    # Cannot reliably determine
    return "unknown"


# ---------------------------------------------------------------------------
# Disturbance → environment + metadata.disturbance split
# ---------------------------------------------------------------------------

def _split_disturbance(dm: dict) -> tuple[dict, dict]:
    """Convert a disturbance_model dict → (environment, metadata.disturbance)."""
    wind_kn = dm.get("wind_kn", 0.0)
    wind_dir = dm.get("wind_dir_nav_deg", 0.0)
    current_kn = dm.get("current_kn", 0.0)
    current_dir = dm.get("current_dir_nav_deg", 0.0)
    vis_m = dm.get("vis_m", 10000.0)
    wave_h = dm.get("wave_height_m", 0.0)

    wind_obj = {"dir_deg": float(wind_dir), "speed_mps": round(float(wind_kn) * KN_TO_MPS, 4)}
    current_obj = {"dir_deg": float(current_dir), "speed_mps": round(float(current_kn) * KN_TO_MPS, 4)}

    environment: dict = {
        "wind": wind_obj,
        "current": current_obj,
        "visibility_nm": round(float(vis_m) * M_TO_NM, 2),
    }
    if float(wave_h) > 0:
        environment["significant_wave_height_m"] = float(wave_h)

    disturbance: dict = {
        "wind": wind_obj,
        "current": current_obj,
    }

    return environment, disturbance


# ---------------------------------------------------------------------------
# V1.0 → maritime-schema
# ---------------------------------------------------------------------------

def _generate_title(doc: dict) -> str:
    """Generate a human-readable title from v1 data."""
    sid = doc.get("scenario_id", "unknown")
    return f"Scenario: {sid}"


def _colregs_rules_from_v1(doc: dict) -> list[str]:
    """Parse rule_branch_covered tokens into COLREGs rule codes."""
    tokens = doc.get("rule_branch_covered", [])
    rules: list[str] = []
    for token in tokens:
        mapped = COLREGS_RULE_TOKEN_MAP.get(token)
        if mapped:
            if mapped not in rules:
                rules.append(mapped)
        else:
            # Fallback: extract number from e.g. "Rule14_HeadOn" → "R14"
            if isinstance(token, str) and token.startswith("Rule"):
                parts = token.split("_")
                num = parts[0][4:]  # strip "Rule" prefix
                code = f"R{num}"
                if code not in rules:
                    rules.append(code)
    return rules


def migrate_v1_to_maritime(
    doc: dict,
    origin: tuple[float, float] = DEFAULT_ORIGIN,
) -> dict:
    """Migrate a v1.0 (ENU-format) YAML document → maritime-schema dict."""
    ic = doc.get("initial_conditions", {})
    os_enu = ic.get("own_ship", {})

    os_lat, os_lon = enu_to_latlon(
        float(os_enu.get("x_m", 0.0)),
        float(os_enu.get("y_m", 0.0)),
        origin,
    )

    start_time = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    dm = doc.get("disturbance_model", {})
    environment, disturbance = _split_disturbance(dm) if dm else ({}, {})

    raw_targets = ic.get("targets", [])
    target_ships: list[dict] = []
    for idx, tgt in enumerate(raw_targets):
        tgt_lat, tgt_lon = enu_to_latlon(
            float(tgt.get("x_m", 0.0)),
            float(tgt.get("y_m", 0.0)),
            origin,
        )
        tid = tgt.get("target_id", idx + 1)
        sog_kn = round(float(tgt.get("sog_mps", 0.0)) * MPS_TO_KN, 2)
        heading = float(tgt.get("cog_nav_deg", 0.0))

        ship: dict = {
            "id": f"ts{tid}",
            "static": {
                "id": idx + 2,  # own ship = 1
            },
            "initial": {
                "position": {"latitude": round(tgt_lat, 6), "longitude": round(tgt_lon, 6)},
                "cog": float(tgt.get("cog_nav_deg", 0.0)),
                "sog": sog_kn,
                "heading": heading,
            },
            "model": "ais_replay_vessel",
        }
        target_ships.append(ship)

    pass_criteria = doc.get("pass_criteria", {})
    sim = doc.get("simulation", {})
    colregs_rules = _colregs_rules_from_v1(doc)
    odd_zone_raw = str(doc.get("odd_zone", "")).upper()

    metadata: dict = {
        "schema_version": SCHEMA_VERSION,
        "scenario_id": str(doc.get("scenario_id", "")),
        "colregs_rules": colregs_rules,
        "vessel_class": str(doc.get("vessel_class", "")),
        "odd_cell": {
            "domain": ODD_ZONE_MAP.get(odd_zone_raw, "open_sea_offshore_wind_farm"),
        },
        "encounter": doc.get("encounter", {}),
    }

    seed = doc.get("prng_seed")
    if seed is not None:
        metadata["seed"] = int(seed)

    if pass_criteria:
        eo: dict = {}
        if "min_dcpa_with_action_m" in pass_criteria:
            eo["cpa_min_m_ge"] = float(pass_criteria["min_dcpa_with_action_m"])
        if eo:
            metadata["expected_outcome"] = eo

    if sim:
        ss: dict = {}
        if "duration_s" in sim:
            ss["total_time"] = float(sim["duration_s"])
        if "dt_s" in sim:
            ss["dt"] = float(sim["dt_s"])
        if "n_rps" in os_enu:
            ss["n_rps_initial"] = float(os_enu["n_rps"])
        ss["dynamics_mode"] = "internal"
        if ss:
            metadata["simulation_settings"] = ss

    if disturbance:
        metadata["disturbance"] = disturbance

    result: dict = {
        "title": _generate_title(doc),
        "description": str(doc.get("description", "")),
        "startTime": start_time,
        "ownShip": {
            "static": {"id": 1, "shipType": "Cargo", "name": "FCB Own Ship"},
            "initial": {
                "position": {"latitude": round(os_lat, 6), "longitude": round(os_lon, 6)},
                "cog": float(os_enu.get("heading_nav_deg", 0.0)),
                "sog": float(os_enu.get("speed_kn", 0.0)),
                "heading": float(os_enu.get("heading_nav_deg", 0.0)),
            },
            "model": "fcb_mmg_vessel",
            "controller": "psbmpc_wrapper",
        },
        "targetShips": target_ships,
        "environment": environment,
        "metadata": metadata,
    }

    return result


# V2.0 → maritime-schema

def _map_v2_id(v2_id: str) -> int:
    """Map v2 own_ship.id string → integer id.
    'os' → 1, otherwise try int conversion, default 1.
    """
    if v2_id == "os":
        return 1
    try:
        return int(v2_id)
    except (ValueError, TypeError):
        return 1


def migrate_v2_to_maritime(doc: dict) -> dict:
    """Migrate a v2.0 (IMAZU-format) YAML document → maritime-schema dict."""
    os_v2 = doc.get("own_ship", {})
    os_init = os_v2.get("initial", {})
    os_pos = os_init.get("position", {})

    meta_in = doc.get("metadata", {})
    dm = meta_in.get("disturbance_model", {})
    pass_criteria = meta_in.get("pass_criteria", {})
    sim = meta_in.get("simulation", {})

    own_ship: dict = {
        "static": {
            "id": _map_v2_id(os_v2.get("id", "os")),
            "shipType": "Cargo",
            "name": "FCB Own Ship",
        },
        "initial": {
            "position": {
                "latitude": float(os_pos.get("latitude", 0.0)),
                "longitude": float(os_pos.get("longitude", 0.0)),
            },
            "cog": float(os_init.get("cog", 0.0)),
            "sog": float(os_init.get("sog", 0.0)),
            "heading": float(os_init.get("heading", 0.0)),
        },
        "model": "fcb_mmg_vessel",
        "controller": "psbmpc_wrapper",
    }

    mmsi = os_v2.get("mmsi")
    if mmsi is not None:
        own_ship["static"]["mmsi"] = int(mmsi)

    ns = os_v2.get("nav_status")
    if ns is not None:
        own_ship["initial"]["navStatus"] = ais_nav_status_int_to_string(int(ns))

    raw_targets = doc.get("target_ships", [])
    target_ships: list[dict] = []
    for idx, tgt in enumerate(raw_targets):
        tgt_init = tgt.get("initial", {})
        tgt_pos = tgt_init.get("position", {})
        tgt_id = tgt.get("id", f"ts{idx + 1}")
        heading = tgt_init.get("heading", tgt_init.get("cog", 0.0))

        ship: dict = {
            "id": str(tgt_id),
            "static": {
                "id": idx + 2,
            },
            "initial": {
                "position": {
                    "latitude": float(tgt_pos.get("latitude", 0.0)),
                    "longitude": float(tgt_pos.get("longitude", 0.0)),
                },
                "cog": float(tgt_init.get("cog", 0.0)),
                "sog": float(tgt_init.get("sog", 0.0)),
                "heading": float(heading),
            },
            "model": "ais_replay_vessel",
        }
        tgt_mmsi = tgt.get("mmsi")
        if tgt_mmsi is not None:
            ship["static"]["mmsi"] = int(tgt_mmsi)
        target_ships.append(ship)

    environment, disturbance = _split_disturbance(dm) if dm else ({}, {})

    odd_zone_raw = str(meta_in.get("odd_zone", "")).upper()
    geo = meta_in.get("geo_origin", {})

    metadata: dict = {
        "schema_version": SCHEMA_VERSION,
        "scenario_id": str(meta_in.get("scenario_id", "")),
        "vessel_class": str(meta_in.get("vessel_class", "")),
        "odd_cell": {
            "domain": ODD_ZONE_MAP.get(odd_zone_raw, "open_sea_offshore_wind_farm"),
        },
        "encounter": meta_in.get("encounter", {}),
    }

    ss_ = meta_in.get("scenario_source")
    if ss_:
        metadata["scenario_source"] = str(ss_)

    seed = meta_in.get("prng_seed")
    if seed is not None:
        metadata["seed"] = int(seed)

    if pass_criteria:
        eo: dict = {}
        if "min_dcpa_with_action_m" in pass_criteria:
            eo["cpa_min_m_ge"] = float(pass_criteria["min_dcpa_with_action_m"])
        if eo:
            metadata["expected_outcome"] = eo

    if sim:
        ss: dict = {}
        if "duration_s" in sim:
            ss["total_time"] = float(sim["duration_s"])
        if "dt_s" in sim:
            ss["dt"] = float(sim["dt_s"])
        if "n_rps_initial" in sim:
            ss["n_rps_initial"] = float(sim["n_rps_initial"])
        if geo:
            ss["coordinate_origin"] = [
                float(geo.get("latitude", 0.0)),
                float(geo.get("longitude", 0.0)),
            ]
        ss["dynamics_mode"] = "internal"
        metadata["simulation_settings"] = ss

    if disturbance:
        metadata["disturbance"] = disturbance

    result: dict = {
        "title": str(doc.get("title", "")),
        "description": str(doc.get("description", "")),
        "startTime": str(doc.get("start_time", "")),
        "ownShip": own_ship,
        "targetShips": target_ships,
        "environment": environment,
        "metadata": metadata,
    }

    return result


# ---------------------------------------------------------------------------
# File I/O helpers
# ---------------------------------------------------------------------------

def _sha256(path: str | Path) -> str:
    """Return hex SHA-256 digest of file contents."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _backup_suffix(doc: dict) -> str:
    """Determine the backup suffix based on detected schema version."""
    ver = detect_schema_version(doc)
    if ver == "v1":
        return ".v1-bak.yaml"
    elif ver == "v2":
        return ".v2-bak.yaml"
    return ".bak.yaml"


# ---------------------------------------------------------------------------
# Migration log
# ---------------------------------------------------------------------------

MIGRATION_LOG = "migration_log.json"


def _append_migration_log(entry: dict) -> None:
    """Append a migration entry to the JSON log file."""
    entries: list[dict] = []
    if os.path.exists(MIGRATION_LOG):
        try:
            with open(MIGRATION_LOG, "r") as f:
                entries = json.load(f)
        except (json.JSONDecodeError, Exception):
            entries = []
    entries.append(entry)
    with open(MIGRATION_LOG, "w") as f:
        json.dump(entries, f, indent=2, ensure_ascii=False)


def _load_migration_log() -> list[dict]:
    """Return the migration log entries."""
    if not os.path.exists(MIGRATION_LOG):
        return []
    try:
        with open(MIGRATION_LOG, "r") as f:
            return json.load(f)
    except Exception:
        return []


# ---------------------------------------------------------------------------
# Single file migration
# ---------------------------------------------------------------------------

def migrate_file(filepath: str, dry_run: bool = False) -> dict:
    """Migrate a single YAML file.

    Steps:
        1. Detect schema version.
        2. SHA-256 before.
        3. If v1/v2 → convert to maritime-schema dict.
        4. Backup original → *.v1-bak.yaml or *.v2-bak.yaml.
        5. Write new YAML.
        6. SHA-256 after.
        7. Append to migration log.
    """
    path = Path(filepath)
    if not path.exists():
        raise FileNotFoundError(f"File not found: {filepath}")

    with open(path, "r") as f:
        doc = yaml.safe_load(f)

    if doc is None:
        raise ValueError(f"Empty YAML file: {filepath}")

    ver = detect_schema_version(doc)
    if ver == "unknown" or ver == "legacy":
        return {"file": filepath, "status": "skipped", "reason": f"unsupported schema version: {ver}"}

    sha_before = _sha256(path)

    if ver == "v1":
        result = migrate_v1_to_maritime(doc)
    elif ver == "v2":
        result = migrate_v2_to_maritime(doc)
    else:
        return {"file": filepath, "status": "skipped", "reason": f"unsupported version: {ver}"}

    if dry_run:
        return {
            "file": filepath,
            "status": "dry_run",
            "version": ver,
            "sha256_before": sha_before,
        }

    # Backup
    backup_path = path.parent / f"{path.stem}{_backup_suffix(doc)}"
    shutil.copy2(path, backup_path)

    # Write
    with open(path, "w") as f:
        yaml.dump(result, f, default_flow_style=False, sort_keys=False, allow_unicode=True, width=120)

    sha_after = _sha256(path)

    entry = {
        "file": str(path),
        "backup": str(backup_path),
        "version": ver,
        "sha256_before": sha_before,
        "sha256_after": sha_after,
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    }
    _append_migration_log(entry)

    return {"file": filepath, "status": "migrated", "version": ver, "sha256_before": sha_before}


# ---------------------------------------------------------------------------
# Rollback
# ---------------------------------------------------------------------------

def _rollback(target: str = "all") -> list[dict]:
    """Restore from backup files.

    If target == "all", scan for *.v1-bak.yaml and *.v2-bak.yaml files.
    Otherwise, restore a single file by finding its backup.
    """
    results: list[dict] = []
    log = _load_migration_log()

    if target == "all":
        for pattern in ("*.v1-bak.yaml", "*.v2-bak.yaml"):
            for bak_path in sorted(Path("scenarios").rglob(pattern)):
                _do_restore(bak_path, results, log)
        for bak_path in sorted(Path("scenarios").rglob("*.bak.yaml")):
            if bak_path.suffixes[-2:] not in ([".v1-bak", ".yaml"], [".v2-bak", ".yaml"]):
                _do_restore(bak_path, results, log)
    else:
        target_path = Path(target)
        for pattern in ("*.v1-bak.yaml", "*.v2-bak.yaml"):
            bak_candidate = target_path.parent / f"{target_path.stem}{pattern[1:]}"
            if bak_candidate.exists():
                _do_restore(bak_candidate, results, log)
                break
        else:
            results.append({"file": target, "status": "error", "reason": "no backup found"})

    return results


def _do_restore(bak_path: Path, results: list[dict], log: list[dict]) -> None:
    """Restore a single backup file."""
    stem = bak_path.stem
    for suffix in (".v1-bak", ".v2-bak", ".bak"):
        if stem.endswith(suffix):
            original_stem = stem[: -len(suffix)]
            break
    else:
        original_stem = stem
    original_path = bak_path.parent / f"{original_stem}.yaml"

    shutil.copy2(bak_path, original_path)
    bak_path.unlink()
    results.append({"file": str(original_path), "status": "restored", "from": str(bak_path)})


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Migrate v1.0/v2.0 scenario YAML → maritime-schema format",
    )
    parser.add_argument("--dry-run", action="store_true", help="Report without writing")
    parser.add_argument("--all", action="store_true", help="Migrate all v1/v2 scenario files")
    parser.add_argument("--file", type=str, help="Single file to migrate")
    parser.add_argument(
        "--rollback",
        type=str,
        nargs="?",
        const="all",
        help="Rollback migration for a file, or 'all' for all",
    )

    args = parser.parse_args()

    if args.rollback:
        results = _rollback(args.rollback)
        for r in results:
            status = r.get("status", "?")
            if status == "restored":
                print(f"  ✅ {r['file']} ← {r['from']}")
            else:
                print(f"  ❌ {r.get('file', '?')}: {r.get('reason', status)}")
        return

    # --file or --all
    files_to_migrate: list[str] = []
    if args.file:
        files_to_migrate.append(args.file)
    elif args.all:
        base = Path("scenarios")
        if not base.exists():
            print("❌ scenarios/ directory not found", file=sys.stderr)
            sys.exit(1)
        for yaml_path in sorted(base.rglob("*.yaml")):
            if "schema/" in str(yaml_path) or ".bak" in str(yaml_path) or yaml_path.name == "schema.yaml":
                continue
            with open(yaml_path) as f:
                try:
                    doc = yaml.safe_load(f)
                except Exception:
                    continue
            if doc and detect_schema_version(doc) in ("v1", "v2"):
                files_to_migrate.append(str(yaml_path))
    else:
        parser.print_help()
        sys.exit(1)

    for fp in files_to_migrate:
        try:
            result = migrate_file(fp, dry_run=args.dry_run)
            status = result.get("status", "?")
            if status == "migrated":
                print(f"  ✅ {fp} → migrated (v{result.get('version', '?')})")
            elif status == "dry_run":
                print(f"  🔍 {fp} → would migrate (v{result.get('version', '?')}) [{result.get('sha256_before', '')[:12]}…]")
            elif status == "skipped":
                print(f"  ⏭️  {fp}: {result.get('reason', 'skipped')}")
            else:
                print(f"  ❓ {fp}: {result}")
        except Exception as e:
            print(f"  ❌ {fp}: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()
