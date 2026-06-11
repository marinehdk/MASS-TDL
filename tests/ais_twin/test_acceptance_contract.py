from pathlib import Path


def test_acceptance_scripts_exist_and_reference_safe_route_config():
    capture = Path("scripts/ais_twin_capture_safe_route.sh")
    replay = Path("scripts/ais_twin_replay_safe_route.sh")

    assert capture.exists()
    assert replay.exists()
    assert "safe_route_aisstream.yaml" in capture.read_text(encoding="utf-8")
    assert "ais_twin_replay_node" in replay.read_text(encoding="utf-8")


def test_replay_script_forwards_extra_args():
    replay = Path("scripts/ais_twin_replay_safe_route.sh")
    assert '"$@"' in replay.read_text(encoding="utf-8")
