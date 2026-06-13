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


def test_acceptance_starts_runtime_profile_services():
    script = (ROOT / "scripts/local-a4000-acceptance.sh").read_text()
    up_line = next(
        line for line in script.splitlines()
        if line.startswith("docker compose up -d --build")
    )

    assert "martin-tile-server" in up_line
    assert "plugin-hydro-fossen" in up_line
    assert "plugin-route-l2-main" in up_line
    assert "plugin-fusion-yougc" in up_line
    assert "plugin-route-tdl-mock" not in up_line


def test_acceptance_gates_runtime_probe_on_go_verdict():
    script = (ROOT / "scripts/local-a4000-acceptance.sh").read_text()
    probe_index = script.index("/api/v1/runtime/probe")
    verdict_index = script.index('"verdict":"GO"', probe_index)

    assert verdict_index > probe_index
