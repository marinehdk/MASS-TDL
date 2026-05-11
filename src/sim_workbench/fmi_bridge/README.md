# fmi_bridge — OSP libcosim FMI 2.0 Bridge + dds-fmu Mediator (D1.3c)

FMI 2.0 Co-Simulation bridge connecting ROS2 Humble DDS topics to FMU variables.

## Quick Start

```bash
# 1. Build the MMG FMU
python -m fmi_bridge.python.build_fmu --output fcb_mmg_4dof.fmu

# 2. Validate FMU modelDescription
python -m pytest test/test_fmu_model_description.py -v

# 3. Run turning circle verification
python -m pytest test/test_fmu_turning_circle.py -v

# 4. Launch fmi_bridge with ROS2
ros2 launch fmi_bridge fmi_bridge.launch.py fmu_path:=fcb_mmg_4dof.fmu
```

## Architecture

- `python/` — pythonfmu FMU model + build CLI
- `src/` — C++ libcosim_wrapper + dds_fmu_bridge + ROS2 node
- `config/` — DDS<->FMU signal mapping YAML

## Phase 1 vs Phase 2

| Feature | Phase 1 | Phase 2 |
|---|---|---|
| FMU packaging | pythonfmu | C++ FMI Library |
| FMU count | 1 (own-ship MMG) | 3+ (MMG + ncdm + disturbance) |
| Target ships | ROS2 native | ncdm_vessel.fmu |
| farn sweep | 10 cells | 100 cells |
