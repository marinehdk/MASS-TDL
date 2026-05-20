"""Unit tests for coverage_cube.py and scenarios_coverage.py."""
from __future__ import annotations

from coverage_cube import (
    CoverageCube,
    COLREG_RULES,
    ODD_ZONES,
    DISTURBANCE_BINS,
    SEEDS,
    TOTAL_CELLS,
    wind_kn_to_bin,
    _normalize_rule,
    seed_index_from_filename,
)


class TestCoverageCube:
    def test_empty_cube(self):
        cube = CoverageCube()
        assert cube.cells_lit() == 0
        m = cube.to_heatmap_matrix()
        assert len(m) == 11
        assert all(len(row) == 4 for row in m)

    def test_mark_one_cell(self):
        cube = CoverageCube()
        cube.mark("Rule14", "open_sea", 2.0, 10000.0, 1)
        assert cube.cells_lit() == 1

    def test_mark_all_seeds(self):
        cube = CoverageCube()
        for s in range(1, 6):
            cube.mark("Rule14", "open_sea", 2.0, 10000.0, s)
        assert cube.cells_lit() == 5

    def test_mark_multiple_rules_and_odds(self):
        cube = CoverageCube()
        cube.mark("Rule14", "open_sea", 2.0, 10000.0, 1)
        cube.mark("Rule13", "coastal_traffic_separation", 5.0, 10000.0, 1)
        cube.mark("Rule15", "port_approach", 15.0, 10000.0, 3)
        assert cube.cells_lit() == 3

    def test_mark_unknown_rule_ignored(self):
        cube = CoverageCube()
        cube.mark("Rule99", "open_sea", 2.0, 10000.0, 1)
        assert cube.cells_lit() == 0

    def test_mark_unknown_odd_defaults(self):
        cube = CoverageCube()
        cube.mark("Rule14", "unknown_zone", 2.0, 10000.0, 1)
        assert cube.cells_lit() == 1

    def test_heatmap_matrix_shape(self):
        cube = CoverageCube()
        cube.mark("Rule14", "open_sea", 2.0, 10000.0, 1)
        cube.mark("Rule13", "coastal_traffic_separation", 5.0, 10000.0, 3)
        m = cube.to_heatmap_matrix()
        assert len(m) == 11
        assert all(len(row) == 4 for row in m)
        # Row indices: Rule13=5, Rule14=6 in COLREG_RULES
        assert m[5][1] >= 1  # Rule13 × coastal_traffic_separation
        assert m[6][0] >= 1  # Rule14 × open_sea

    def test_mark_keyword_rule5_lookout(self):
        cube = CoverageCube()
        cube.mark("lookout scenario", "open_sea", 2.0, 10000.0, 1)
        assert cube.cells_lit() == 1

    def test_mark_keyword_rule19_restrvis(self):
        cube = CoverageCube()
        cube.mark("restricted visibility test", "port_approach", 25.0, 3000.0, 1)
        assert cube.cells_lit() == 1

    def test_mark_same_cell_dedup(self):
        cube = CoverageCube()
        cube.mark("Rule14", "open_sea", 2.0, 10000.0, 1)
        cube.mark("Rule14", "open_sea", 2.0, 10000.0, 1)
        assert cube.cells_lit() == 1

    def test_mark_dedup_different_disturbance(self):
        """Same (rule, odd, seed) but different wind speeds → same cell bin."""
        cube = CoverageCube()
        cube.mark("Rule14", "open_sea", 1.0, 10000.0, 1)
        cube.mark("Rule14", "open_sea", 3.0, 10000.0, 1)
        assert cube.cells_lit() == 1  # both map to bf_0_1

    def test_to_json_dict(self):
        cube = CoverageCube()
        cube.mark("Rule14", "open_sea", 2.0, 10000.0, 1)
        d = cube.to_json_dict()
        assert d["cells_lit"] == 1
        assert d["total_cells"] == 1100


class TestConstants:
    def test_total_cells_const(self):
        assert TOTAL_CELLS == 1100

    def test_all_rules_constant(self):
        expected = {"Rule5", "Rule6", "Rule7", "Rule8", "Rule9",
                    "Rule13", "Rule14", "Rule15", "Rule16", "Rule17", "Rule19"}
        assert set(COLREG_RULES) == expected
        assert len(COLREG_RULES) == 11

    def test_all_odd_zones_constant(self):
        assert len(ODD_ZONES) == 4
        assert "offshore_wind_farm" in ODD_ZONES

    def test_all_disturbance_bins_constant(self):
        assert len(DISTURBANCE_BINS) == 5
        assert "sensor_degraded" in DISTURBANCE_BINS

    def test_all_seeds_constant(self):
        assert len(SEEDS) == 5
        assert SEEDS == [1, 2, 3, 4, 5]

    def test_total_cells_arithmetic(self):
        assert (len(COLREG_RULES) * len(ODD_ZONES) * len(DISTURBANCE_BINS) * len(SEEDS)) == 1100


class TestWindToBin:
    def test_sensor_degraded_by_vis(self):
        assert wind_kn_to_bin(5.0, 2000.0) == "sensor_degraded"

    def test_bf_0_1(self):
        assert wind_kn_to_bin(1.0, 10000.0) == "bf_0_1"

    def test_bf_2_3(self):
        assert wind_kn_to_bin(5.0, 10000.0) == "bf_2_3"

    def test_bf_4_5(self):
        assert wind_kn_to_bin(15.0, 10000.0) == "bf_4_5"

    def test_bf_6_7(self):
        assert wind_kn_to_bin(25.0, 10000.0) == "bf_6_7"

    def test_boundary_bf_0_1_upper(self):
        assert wind_kn_to_bin(3.4, 10000.0) == "bf_0_1"

    def test_boundary_bf_2_3_lower(self):
        assert wind_kn_to_bin(3.5, 10000.0) == "bf_2_3"

    def test_sensor_degraded_overrides_wind(self):
        """Visibility < 5000 m always yields sensor_degraded regardless of wind."""
        assert wind_kn_to_bin(25.0, 4999.0) == "sensor_degraded"

    def test_exact_vis_boundary(self):
        assert wind_kn_to_bin(5.0, 5000.0) != "sensor_degraded"
        assert wind_kn_to_bin(5.0, 4999.0) == "sensor_degraded"


class TestNormalizeRule:
    def test_numeric_rule(self):
        assert _normalize_rule("Rule 14 Head-on") == "Rule14"

    def test_rule13_label(self):
        assert _normalize_rule("Rule13") == "Rule13"

    def test_lookout_keyword(self):
        assert _normalize_rule("lookout scenario") == "Rule5"

    def test_lookout_with_dash(self):
        assert _normalize_rule("look-out test") == "Rule5"

    def test_safe_speed_keyword(self):
        assert _normalize_rule("safe_speed_test") == "Rule6"

    def test_risk_of_collision_keyword(self):
        assert _normalize_rule("roc check") == "Rule7"

    def test_risk_of_collision_full(self):
        assert _normalize_rule("risk of collision scenario") == "Rule7"

    def test_action_to_avoid_keyword(self):
        assert _normalize_rule("actionavoid maneuver") == "Rule8"

    def test_narrow_channel_keyword(self):
        assert _normalize_rule("narrow channel test") == "Rule9"

    def test_narrow_alone(self):
        assert _normalize_rule("narrow") == "Rule9"

    def test_restricted_visibility_keyword(self):
        assert _normalize_rule("restrvis fog") == "Rule19"

    def test_restricted_visibility_full(self):
        assert _normalize_rule("restricted visibility scenario") == "Rule19"

    def test_unknown_rule_returns_original(self):
        assert _normalize_rule("bogus_rule") == "bogus_rule"

    def test_empty_string(self):
        assert _normalize_rule("") == ""

    def test_rule18_not_in_colreg_set(self):
        """Rule18 is not in COLREG_RULES, so numeric extraction returns original."""
        assert _normalize_rule("Rule18") == "Rule18"


class TestSeedIndex:
    def test_explicit_seed(self):
        assert seed_index_from_filename("scenario_seed3") == 3  # (3-1)%5+1 = 3

    def test_no_seed_defaults(self):
        assert seed_index_from_filename("plain_scenario") == 1

    def test_seed1_to_1(self):
        assert seed_index_from_filename("scenario_seed1") == 1  # (1-1)%5+1 = 1

    def test_seed5_to_5(self):
        assert seed_index_from_filename("scenario_seed5") == 5  # (5-1)%5+1 = 5

    def test_seed_10_wraps_to_5(self):
        assert seed_index_from_filename("scenario_seed10") == 5  # (10-1)%5+1 = 5

    def test_seed_7_wraps_to_2(self):
        assert seed_index_from_filename("scenario_seed7") == 2  # (7-1)%5+1 = 2


class TestCoverageCubeLoadCsv:
    def test_load_from_csv(self, tmp_path):
        csv_content = (
            "scenario_id,rule,odd_zone,wind_kn,vis_m,seed\n"
            "s1,Rule 14 Head-on,open_sea,2.0,10000,1\n"
            "s2,Rule13,coastal_traffic_separation,5.0,5000,3\n"
        )
        csv_path = tmp_path / "traceability.csv"
        csv_path.write_text(csv_content)
        cube = CoverageCube.load_from_csv(str(csv_path))
        assert cube.cells_lit() >= 1

    def test_load_empty_csv(self, tmp_path):
        csv_content = "scenario_id,rule,odd_zone,wind_kn,vis_m,seed\n"
        csv_path = tmp_path / "empty.csv"
        csv_path.write_text(csv_content)
        cube = CoverageCube.load_from_csv(str(csv_path))
        assert cube.cells_lit() == 0

    def test_load_csv_keyword_rules(self, tmp_path):
        csv_content = (
            "scenario_id,rule,odd_zone,wind_kn,vis_m,seed\n"
            "s1,lookout scenario,open_sea,2.0,10000,1\n"
            "s2,restrvis scenario,port_approach,25.0,3000,2\n"
        )
        csv_path = tmp_path / "keyword.csv"
        csv_path.write_text(csv_content)
        cube = CoverageCube.load_from_csv(str(csv_path))
        assert cube.cells_lit() == 2
