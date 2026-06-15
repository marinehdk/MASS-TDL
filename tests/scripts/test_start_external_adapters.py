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


def test_local_a4000_env_uses_external_plugin_runtime_defaults():
    result = subprocess.run(
        [
            "bash",
            "-lc",
            "source scripts/local-a4000-env.sh && "
            'printf "%s\n%s\n%s\n%s\n%s\n%s\n%s" '
            '"$COMPOSE_FILE" "$COMPOSE_PROFILES" "$TDL_INTEGRATION_PROFILE" '
            '"$TDL_RUNTIME_PROFILE" "$ORCH_PORT" "$FOX_PORT" "$SIL_NODES_CPUS"',
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert result.stdout.splitlines() == [
        "docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml",
        "plugins",
        "a4000_external",
        "integration-local",
        "18000",
        "18765",
        "4.0",
    ]


def test_local_a4000_acceptance_dry_run_prints_gate_order():
    result = subprocess.run(
        ["bash", "scripts/local-a4000-acceptance.sh", "--dry-run"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    assert result.returncode == 0
    assert "compose=docker-compose.yml:docker-compose.a4000.yml:docker-compose.plugins.yml" in result.stdout
    assert "profiles=plugins" in result.stdout
    assert "integration_profile=a4000_external" in result.stdout
    assert "runtime_profile=integration-local" in result.stdout
    assert "health=https://127.0.0.1:18000/api/v1/health" in result.stdout
    assert "integration=/api/v1/integration/profiles" in result.stdout
    assert "domain=42" in result.stdout
    assert "certs=certs/sil.crt certs/sil.key" in result.stdout


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
    assert "plugin-route-l2-main" in result.stdout


def test_sil_entrypoint_starts_external_adapters_only_for_external_profile():
    source = Path("docker/sil_entrypoint.sh").read_text(encoding="utf-8")

    assert 'TDL_INTEGRATION_PROFILE:-default' in source
    assert 'start_external_adapters.sh &' in source


def test_sil_entrypoint_disables_internal_l2_route_sources_for_external_profile():
    source = Path("docker/sil_entrypoint.sh").read_text(encoding="utf-8")

    assert "external_l2_route =" in source
    assert "SIL_MOCK_L2_ROUTE_ENABLE" in source
    assert "gnc_route_proc = None" in source
    assert "route_ingest_proc = None" in source
    assert "external L2 route profile: skipping internal GNC route mock and route ingest" in source


def test_mock_l2_can_disable_route_and_speed_publishers():
    source = Path("docker/mock_l2_publisher.py").read_text(encoding="utf-8")

    assert "SIL_MOCK_L2_ROUTE_ENABLE" in source
    assert "self._mock_route_enabled" in source
    assert "mock L2 route publishing disabled" in source


def test_local_acceptance_generates_dev_tls_certs():
    source = Path("scripts/local-a4000-acceptance.sh").read_text(encoding="utf-8")

    assert "openssl req -x509" in source
    assert "certs/sil.crt" in source
    assert "certs/sil.key" in source


def test_sil_nodes_image_builds_external_adapter_package_and_scripts():
    source = Path("docker/sil_nodes.Dockerfile").read_text(encoding="utf-8")

    assert "src/sim_workbench/external_adapters" in source
    assert "external_adapters" in source
    assert "scripts/integration/start_external_adapters.sh" in source


def test_external_adapters_install_console_scripts_for_ros2_run():
    source = Path("src/sim_workbench/external_adapters/setup.cfg").read_text(encoding="utf-8")

    assert "script_dir=$base/lib/external_adapters" in source
    assert "install_scripts=$base/lib/external_adapters" in source


def test_orchestrator_image_copies_integration_profiles_to_runtime_path():
    source = Path("docker/sil_orchestrator.Dockerfile").read_text(encoding="utf-8")

    assert "config/integration_profiles" in source
    assert "/opt/config/integration_profiles" in source
    assert "FROM ros:humble-ros-base" in source
    assert "mass-l3/ci" not in source
