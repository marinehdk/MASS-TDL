import subprocess
from pathlib import Path


SCRIPT = Path("scripts/integration/start_external_adapters.sh")


def test_default_profile_prints_disabled():
    result = subprocess.run(
        ["bash", str(SCRIPT)],
        env={"TDL_INTEGRATION_PROFILE": "default"},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert "external adapters disabled for profile=default" in result.stdout


def test_external_profile_prints_expected_process_names_and_domains():
    result = subprocess.run(
        ["bash", str(SCRIPT), "--dry-run"],
        env={"TDL_INTEGRATION_PROFILE": "a4000_external"},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert "external_tdl_ingress" in result.stdout
    assert "external_route_out_tdl" in result.stdout
    assert "external_route_out_path" in result.stdout
    assert "ROS_DOMAIN_ID=42" in result.stdout
    assert "ROS_DOMAIN_ID=10" in result.stdout


def test_local_a4000_env_reuses_a4000_compose_override():
    result = subprocess.run(
        [
            "bash",
            "-lc",
            'source scripts/local-a4000-env.sh && printf "%s %s %s" "$COMPOSE_FILE" "$ORCH_PORT" "$FOX_PORT"',
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert result.stdout == "docker-compose.yml:docker-compose.a4000.yml 18000 18765"


def test_local_a4000_acceptance_dry_run_prints_gate_order():
    result = subprocess.run(
        ["bash", "scripts/local-a4000-acceptance.sh", "--dry-run"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert "compose=docker-compose.yml:docker-compose.a4000.yml" in result.stdout
    assert "health=https://127.0.0.1:18000/api/v1/health" in result.stdout
    assert "integration=/api/v1/integration/profiles" in result.stdout
    assert "domain=42" in result.stdout


def test_local_a4000_compose_passes_profile_to_sil_nodes():
    result = subprocess.run(
        [
            "bash",
            "-lc",
            "source scripts/local-a4000-env.sh && "
            "TDL_INTEGRATION_PROFILE=a4000_external docker compose config",
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert "TDL_INTEGRATION_PROFILE: a4000_external" in result.stdout


def test_sil_entrypoint_starts_external_adapters_only_for_external_profile():
    source = Path("docker/sil_entrypoint.sh").read_text(encoding="utf-8")

    assert 'TDL_INTEGRATION_PROFILE:-default' in source
    assert 'start_external_adapters.sh &' in source


def test_sil_nodes_image_builds_external_adapter_package_and_scripts():
    source = Path("docker/sil_nodes.Dockerfile").read_text(encoding="utf-8")

    assert "src/sim_workbench/external_adapters" in source
    assert "external_adapters" in source
    assert "scripts/integration/start_external_adapters.sh" in source


def test_orchestrator_image_copies_integration_profiles_to_runtime_path():
    source = Path("docker/sil_orchestrator.Dockerfile").read_text(encoding="utf-8")

    assert "config/integration_profiles" in source
    assert "/opt/config/integration_profiles" in source
