"""Full pipeline orchestrator: raw AIS -> maritime-schema encounter YAML files."""
from __future__ import annotations

from pathlib import Path

from scenario_authoring.authoring.ais_source import load_ais_dataset
from scenario_authoring.pipeline.stage1_group import group_by_mmsi
from scenario_authoring.pipeline.stage2_resample import split_into_segments
from scenario_authoring.pipeline.stage3_smooth import smooth_segment
from scenario_authoring.pipeline.stage4_filter import filter_encounters
from scenario_authoring.pipeline.stage5_export import export_encounter_to_yaml


def extract_encounters(
    data_dir: Path,
    geo_origin: tuple[float, float] = (63.0, 5.0),
    own_mmsi: int | None = None,
) -> list[Path]:
    """Full pipeline: AIS CSV -> maritime-schema encounter YAML files.

    Returns list of YAML file paths.
    """
    records = load_ais_dataset(data_dir)
    if not records:
        print("No valid AIS records found.")
        return []

    grouped = group_by_mmsi(records)
    if len(grouped) < 2:
        print(f"Need >= 2 vessels with >=4 records, got {len(grouped)}")
        return []

    # Own-ship selection
    if own_mmsi is not None and own_mmsi in grouped:
        own_recs = grouped.pop(own_mmsi)
    else:
        own_mmsi = max(grouped.keys(), key=lambda m: len(grouped[m]))
        own_recs = grouped.pop(own_mmsi)

    # Stage 2: Split into segments
    own_segments = split_into_segments(own_recs)
    all_target_segments: list = []
    for _mmsi, recs in grouped.items():
        all_target_segments.extend(split_into_segments(recs))

    # Stage 3: Smooth
    own_segments = [smooth_segment(s) for s in own_segments if len(s.t) >= 21]
    all_target_segments = [
        smooth_segment(s) for s in all_target_segments if len(s.t) >= 21
    ]

    if not own_segments or not all_target_segments:
        print("No valid segments after smoothing.")
        return []

    # Stage 4: Filter encounters
    encounters = filter_encounters(own_segments, all_target_segments)

    # Stage 5: Export YAML
    yaml_paths = []
    for enc in encounters:
        yp = export_encounter_to_yaml(enc, geo_origin=geo_origin)
        yaml_paths.append(yp)

    return yaml_paths


def main() -> None:
    """CLI entry point."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Extract COLREGs encounters from AIS data"
    )
    parser.add_argument("data_dir", type=Path, help="Directory with AIS CSV files")
    parser.add_argument(
        "--own-mmsi", type=int, help="Own-ship MMSI (default: vessel with most records)"
    )
    parser.add_argument("--geo-origin-lat", type=float, default=63.0)
    parser.add_argument("--geo-origin-lon", type=float, default=5.0)
    args = parser.parse_args()

    paths = extract_encounters(
        args.data_dir,
        geo_origin=(args.geo_origin_lat, args.geo_origin_lon),
        own_mmsi=args.own_mmsi,
    )
    print(f"\nExtracted {len(paths)} encounters:")
    for p in paths:
        print(f"  {p}")


if __name__ == "__main__":
    main()
