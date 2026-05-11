# scenario_authoring — AIS-driven Scenario Authoring (D1.3b.2)

5-stage pipeline turning raw AIS data into maritime-schema COLREGs encounter YAML files.

## Quick Start

```bash
# 1. Download AIS data  
python scripts/download_noaa.py --year 2024 --month 1 --zone 10
# or
python scripts/download_kystverket.py --year 2024 --month 6 --day 15

# 2. Validate dataset
python scripts/validate_dataset.py data/ais_datasets/noaa/

# 3. Extract encounters
python -m scenario_authoring.authoring.encounter_extractor data/ais_datasets/noaa/ \
    --own-mmsi 123456789 --geo-origin-lat 37.8 --geo-origin-lon -122.5

# 4. Replay encounter (ROS2)
ros2 launch scenario_authoring scenario_authoring.launch.py \
    scenario_yaml:=scenarios/ais_derived/ais-987654321-20240115-head_on-v1.0.yaml
```

## Package Structure

- `pipeline/` — 5-stage data processing (group -> resample -> smooth -> filter -> export)
- `authoring/` — pipeline orchestrator + data source adapters  
- `replay/` — ROS2 50Hz replay node + target ship modes

## Downstream Consumers

- D1.3b.1: YAML scenarios go to batch_runner
- D1.3b.3: `/world_model/tracks` topic consumed by Web HMI (DEMO-2)
- D2.4: `NcdmVessel` implementation (Ornstein-Uhlenbeck)
- D3.6: `IntelligentVessel` implementation (multi-agent VO/MPC)
