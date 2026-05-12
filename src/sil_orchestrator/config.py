"""SIL Orchestrator configuration.

Resolves storage paths with a 3-step fallback so the same code works under
docker (/var/sil/*) and local macOS dev (./scenarios + ./runs + ./exports):
  1. explicit env var (SIL_SCENARIO_DIR etc.)
  2. /var/sil/{kind} if the parent /var/sil exists and is writable
  3. project-relative ./scenarios (with imazu22 fallback) | ./runs | ./exports
"""
import os
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[2]
_DOCKER_BASE = Path("/var/sil")


def _resolve(env_var: str, kind: str, local_default: Path) -> Path:
    explicit = os.getenv(env_var)
    if explicit:
        return Path(explicit)
    docker_path = _DOCKER_BASE / kind
    if _DOCKER_BASE.exists() and os.access(_DOCKER_BASE, os.W_OK):
        return docker_path
    return local_default


SCENARIO_DIR = _resolve(
    "SIL_SCENARIO_DIR", "scenarios", _PROJECT_ROOT / "scenarios"
)
RUN_DIR = _resolve("SIL_RUN_DIR", "runs", _PROJECT_ROOT / "runs")
EXPORT_DIR = _resolve("SIL_EXPORT_DIR", "exports", _PROJECT_ROOT / "exports")
ROS_DOMAIN_ID = int(os.getenv("ROS_DOMAIN_ID", "0"))
