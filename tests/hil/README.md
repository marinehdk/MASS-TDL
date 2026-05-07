# HIL Scenario Generator — E3-2

Parameterised COLREGs encounter scenario generator for MASS L3 Tactical Layer Hardware-in-the-Loop (HIL) testing.

## Overview

`generate_scenarios.py` produces JSON scenario files that describe initial conditions (own-ship state, target vessels) and expected COLREGs outcomes for a given encounter geometry. Scenarios are consumed by the future **E3-4 HIL test runner**, which injects targets into the `fcb_simulator` ROS2 node and evaluates whether the L3 tactical layer issues the correct avoidance action.

## Files

| File | Purpose |
|---|---|
| `generate_scenarios.py` | Main generator script (CLI) |
| `scenario_schema.py` | Python dataclass definitions for the scenario data model |
| `scenarios/` | Generated output — **git-ignored**, regenerate locally |

## Usage

Run from the repository root or from this directory:

```bash
# Default: ≥1000 scenarios, output to tests/hil/scenarios/
python tests/hil/generate_scenarios.py

# Custom count and output directory
python tests/hil/generate_scenarios.py --count 1000 --output-dir tests/hil/scenarios/
```

Or run as a standalone script from within the `tests/hil/` directory:

```bash
cd tests/hil
python generate_scenarios.py --count 1000 --output-dir scenarios/
```

## Output files

| File | Contents |
|---|---|
| `scenarios/scenarios_all.json` | Full scenario array (≥1000 scenarios) |
| `scenarios/scenarios_100.json` | Curated 100-scenario representative subset (one per rule×geometry cell) |

Verify the output:

```bash
python -c "import json; d=json.load(open('tests/hil/scenarios/scenarios_all.json')); print(len(d))"
python -c "import json; d=json.load(open('tests/hil/scenarios/scenarios_100.json')); print(len(d))"
```

## Scenario coverage

| Rule | Geometry | Count | Own role |
|---|---|---|---|
| Rule 13 | Overtaking (aft sector 112.5°–247.5°) | ≥150 | give_way |
| Rule 14 | Head-on (±5° offset, 500–3000m) | ≥150 | both_give_way |
| Rule 15 | Crossing from starboard (045–135°) | ≥200 | give_way |
| Rule 17 | Stand-on (target to port, altering away) | ≥150 | stand_on |
| Rule 18 | Motor vs fishing/restricted vessel | ≥150 | give_way |
| Rule 19 | Restricted visibility (vis=0.3nm, DEGRADED) | ≥100 | both_give_way |
| Fault injection | DEGRADED/CRITICAL ODD modes | ≥100 | — |

## Coordinate conventions

- `OwnShipInit.psi_deg`: math convention — 0=East, 90=North (matches `fcb_simulator` ENU frame)
- `TargetShipInit.cog_deg`: meteorological convention — 0=North, 90=East (standard maritime)
- All angles in degrees, speeds in knots, distances in metres
- Own ship is always placed at origin (0, 0)

## Dependencies

No external pip packages required. Python 3.10+ stdlib only:
`dataclasses`, `json`, `math`, `random`, `argparse`, `pathlib`

## Reproducibility

`random.seed(42)` is set at module load time. The same `--count` value always produces the same scenarios.

## Integration with HIL test runner (E3-4)

The future E3-4 HIL test runner will:
1. Load `scenarios_all.json` (or a filtered subset)
2. For each scenario, configure `fcb_simulator` initial conditions and inject target tracks via `/fusion/tracked_targets`
3. Run the L3 stack and capture M5 Tactical Planner output
4. Compare actual action against `expected.m5_expected_action`
5. Report pass/fail per scenario and aggregate coverage statistics
