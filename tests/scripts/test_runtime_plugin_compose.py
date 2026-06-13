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
