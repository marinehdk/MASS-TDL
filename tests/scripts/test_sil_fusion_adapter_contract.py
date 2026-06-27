from pathlib import Path


def test_sil_fusion_adapter_relays_ownship_into_m2_fusion_topic():
    source = Path("src/sim_workbench/sil_fusion_adapter/src/sil_fusion_adapter_node.cpp").read_text(
        encoding="utf-8"
    )

    assert "/sil/own_ship_state" in source
    assert "/fusion/own_ship_state" in source
    assert "own_ship_sil_to_l3" in source


def test_sil_fusion_translators_expose_ownship_mapping():
    header = Path("src/sim_workbench/sil_fusion_adapter/include/sil_fusion_adapter/translators.hpp").read_text(
        encoding="utf-8"
    )
    tests = Path("src/sim_workbench/sil_fusion_adapter/test/test_translators.cpp").read_text(
        encoding="utf-8"
    )

    assert "FilteredOwnShipState own_ship_sil_to_l3" in header
    assert "OwnShipSilToL3" in tests
