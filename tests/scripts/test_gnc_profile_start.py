import subprocess
from pathlib import Path


def test_gnc_launcher_marks_l3_runtime_profile_as_gnc():
    source = Path("scripts/gnc-profile-start.sh").read_text(encoding="utf-8")

    assert 'export TDL_RUNTIME_PROFILE="gnc"' in source


def test_gnc_launcher_starts_gnc_stack_before_l3_liveness_wait():
    source = Path("scripts/gnc-profile-start.sh").read_text(encoding="utf-8")

    gnc_start = source.index("starting GNC stack")
    l3_start = source.index("starting L3 sil-nodes stack")
    assert gnc_start < l3_start


def test_a4000_override_passes_runtime_profile_to_sil_nodes():
    result = subprocess.run(
        [
            "bash",
            "-lc",
            "source scripts/local-a4000-env.sh && "
            "TDL_RUNTIME_PROFILE=gnc COMPOSE_PROJECT_NAME=codex-gnc-validation docker compose config sil-nodes",
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert "TDL_RUNTIME_PROFILE: gnc" in result.stdout


def test_a4000_override_passes_compose_project_to_orchestrator_runtime_console():
    result = subprocess.run(
        [
            "bash",
            "-lc",
            "source scripts/local-a4000-env.sh && "
            "TDL_RUNTIME_PROFILE=gnc COMPOSE_PROJECT_NAME=codex-gnc-validation "
            "docker compose config sil-orchestrator",
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert "COMPOSE_PROJECT_NAME: codex-gnc-validation" in result.stdout


def test_sil_entrypoint_uses_gnc_ownship_source_without_ship_dynamics():
    source = Path("docker/sil_entrypoint.sh").read_text(encoding="utf-8")

    assert "external_own_ship_source" in source
    assert "skipping ShipDynamicsNode; /sil/own_ship_state owned by GNC bridge" in source
    assert "if not external_own_ship_source:" in source
    assert "sil_node_classes.insert(0, ShipDynamicsNode)" in source
