"""Unit tests for CoverageCube (V&V Plan §4.2, gate E1.4)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[2] / "tools" / "sil"))


def test_coverage_cube_total_cells():
    from coverage_cube import TOTAL_CELLS
    assert TOTAL_CELLS == 1100


def test_coverage_cube_mark_and_count():
    from coverage_cube import CoverageCube
    cube = CoverageCube()
    assert cube.cells_lit() == 0

    cube.mark(rule="Rule14", odd_zone="open_sea", wind_kn=0.0, vis_m=10000.0, seed_index=1)
    assert cube.cells_lit() == 1

    # Same cell again → still 1
    cube.mark(rule="Rule14", odd_zone="open_sea", wind_kn=0.0, vis_m=10000.0, seed_index=1)
    assert cube.cells_lit() == 1

    # Different seed → new cell
    cube.mark(rule="Rule14", odd_zone="open_sea", wind_kn=0.0, vis_m=10000.0, seed_index=2)
    assert cube.cells_lit() == 2


def test_coverage_cube_unknown_rule_not_counted():
    from coverage_cube import CoverageCube
    cube = CoverageCube()
    cube.mark(rule="RuleXYZ", odd_zone="open_sea", wind_kn=0.0, vis_m=10000.0, seed_index=1)
    assert cube.cells_lit() == 0


def test_coverage_cube_disturbance_bins():
    from coverage_cube import wind_kn_to_bin
    assert wind_kn_to_bin(0.0, 10000.0) == "bf_0_1"
    assert wind_kn_to_bin(5.0, 10000.0) == "bf_2_3"
    assert wind_kn_to_bin(15.0, 10000.0) == "bf_4_5"
    assert wind_kn_to_bin(25.0, 10000.0) == "bf_6_7"
    assert wind_kn_to_bin(0.0, 4000.0) == "sensor_degraded"


def test_coverage_cube_to_json_dict():
    from coverage_cube import CoverageCube
    cube = CoverageCube()
    cube.mark("Rule14", "open_sea", 0.0, 10000.0, 1)
    d = cube.to_json_dict()
    assert d["cells_lit"] == 1
    assert d["total_cells"] == 1100


def test_seed_index_from_filename():
    from coverage_cube import seed_index_from_filename
    assert seed_index_from_filename("colreg-rule14-ho-001-seed42-v1.0") == 3  # 42%5+1=3
    assert seed_index_from_filename("colreg-rule15-cs-001-seed10-v1.0") == 1  # 10%5+1=1
    assert seed_index_from_filename("no-seed-in-name") == 1                   # fallback