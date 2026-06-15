import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_plugin_compose_file_is_valid_with_local_env():
    result = subprocess.run(
        ["bash", "-lc", "source scripts/local-a4000-env.sh && docker compose config -q"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=30,
    )

    assert result.returncode == 0, result.stderr


def test_local_env_includes_plugin_compose():
    result = subprocess.run(
        ["bash", "-lc", "source scripts/local-a4000-env.sh && printf '%s' \"$COMPOSE_FILE\""],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=10,
    )

    assert "docker-compose.plugins.yml" in result.stdout


def test_orchestrator_image_packages_runtime_configs():
    dockerfile = (ROOT / "docker" / "sil_orchestrator.Dockerfile").read_text()

    assert "COPY config/runtime_plugins /opt/config/runtime_plugins" in dockerfile
    assert "COPY config/runtime_profiles /opt/config/runtime_profiles" in dockerfile


def test_a4000_orchestrator_override_mounts_docker_socket_for_runtime_console():
    compose = (ROOT / "docker-compose.a4000.yml").read_text()

    assert "/var/run/docker.sock:/var/run/docker.sock" in compose


def test_base_compose_does_not_mount_docker_socket():
    compose = (ROOT / "docker-compose.yml").read_text()

    assert "/var/run/docker.sock:/var/run/docker.sock" not in compose


def test_acceptance_dry_run_reports_runtime_probe():
    result = subprocess.run(
        ["bash", "scripts/local-a4000-acceptance.sh", "--dry-run"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=10,
    )

    assert result.returncode == 0
    assert "runtime=/api/v1/runtime/summary" in result.stdout
    assert "runtime_probe=/api/v1/runtime/probe" in result.stdout
    assert "reclaim_stale_project=0" in result.stdout


def test_acceptance_starts_runtime_profile_services():
    script = (ROOT / "scripts/local-a4000-acceptance.sh").read_text()

    assert 'up_args=(up -d --build)' in script
    assert 'docker compose "${up_args[@]}"' in script
    assert "martin-tile-server" in script
    assert "plugin-hydro-fossen" in script
    assert "plugin-route-l2-main" in script
    assert "plugin-fusion-yougc" in script
    assert "plugin-route-tdl-mock" not in next(
        line for line in script.splitlines()
        if line.startswith('docker compose "${up_args[@]}"')
    )


def test_acceptance_recreates_when_project_points_to_other_checkout():
    script = (ROOT / "scripts/local-a4000-acceptance.sh").read_text()

    assert 'com.docker.compose.project.working_dir' in script
    assert '"$existing_roots" != "$current_root"' in script
    assert "RECLAIM_STALE_LOCAL_PROJECT" in script
    assert "exit 2" in script
    assert "recreate_project=1" in script
    assert 'up_args+=(--force-recreate)' in script


def test_acceptance_precreates_inactive_plugin_container_for_switching():
    script = (ROOT / "scripts/local-a4000-acceptance.sh").read_text()

    assert "docker compose create --force-recreate plugin-route-tdl-mock" in script
    assert "docker compose create --no-recreate plugin-route-tdl-mock" in script
    assert "docker compose stop plugin-route-tdl-mock" in script


def test_acceptance_gates_runtime_probe_on_go_verdict():
    script = (ROOT / "scripts/local-a4000-acceptance.sh").read_text()
    probe_index = script.index("/api/v1/runtime/probe")
    verdict_index = script.index('"verdict":"GO"', probe_index)

    assert verdict_index > probe_index


def test_l2_plugin_compose_builds_real_plugin_image():
    import yaml

    compose = yaml.safe_load((ROOT / "docker-compose.plugins.yml").read_text())
    service = compose["services"]["plugin-route-l2-main"]

    assert service["image"] == "mass-l2-planner:main"
    build = service["build"]
    assert build["context"] == "."
    assert build["dockerfile"] == "docker/l2_external_plugin.Dockerfile"
    assert service["command"] == ["/opt/l2_entrypoint.sh"]
    assert "while true" not in " ".join(service["command"])
    assert service["network_mode"] == "host"
    assert service["profiles"] == ["plugins"]


def test_l2_plugin_compose_sets_active_route_seed_environment():
    import yaml

    compose = yaml.safe_load((ROOT / "docker-compose.plugins.yml").read_text())
    environment = compose["services"]["plugin-route-l2-main"].get("environment")

    assert isinstance(environment, list)
    assert "ROS_DOMAIN_ID=${ROS_DOMAIN_ID:-42}" in environment
    assert "TDL_INGRESS_HOST=127.0.0.1" in environment
    assert "TDL_INGRESS_PORT=8765" in environment
    assert "L2_ROUTE_STRICT_ACTIVE=1" in environment
    assert "L2_ROUTE_REMOVE_ON_START=1" in environment
    assert "L2_SCENARIO_YAML=/var/sil/scenarios/集成测试/safe_route.yaml" in environment


def test_l2_manifest_matches_external_route_plan_topic_and_domain():
    import yaml

    manifest = yaml.safe_load(
        (ROOT / "config/runtime_plugins/l2-planner-main.yaml").read_text()
    )

    assert manifest["compose"]["service"] == "plugin-route-l2-main"
    assert manifest["image"]["expected"] == "mass-l2-planner:main"
    assert manifest["ros"]["domain_id"] == 42
    assert manifest["ros"]["required_topics"] == {
        "/route_planning/route_plan": "ship_interfaces/msg/RoutePlan"
    }
    assert "/sil/actuator_cmd" in manifest["ros"]["forbidden_topics"]
    assert "/l4/control_cmd" in manifest["ros"]["forbidden_topics"]


def test_l2_external_probe_observes_source_and_tdl_route_topics():
    script_path = ROOT / "scripts/integration/probe_l2_external_plugin.sh"

    assert script_path.is_file()

    script = script_path.read_text()
    executable_script = "\n".join(
        line for line in script.splitlines()
        if not line.lstrip().startswith("#")
    )

    assert re.search(
        r"docker\s+compose\s+exec\s+-T\s+plugin\-route\-l2\-main\b"
        r"(?:(?:[^\n]*\\\n)|[^\n])*"
        r"/route_planning/route_plan",
        executable_script,
    )
    assert re.search(
        r"docker\s+compose\s+exec\s+-T\s+sil\-nodes\b"
        r"(?:(?:[^\n]*\\\n)|[^\n])*"
        r"/l2/planned_route",
        executable_script,
    )
    assert "L2_EXTERNAL_PLUGIN_PROBE_PASS" in script
