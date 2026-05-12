"""Scenario Authoring Node — YAML CRUD, Imazu-22, AIS pipeline."""
import hashlib
import uuid
from pathlib import Path


class ScenarioAuthoringNode:
    """Filesystem-backed YAML scenario CRUD with SHA-256 content hash."""

    def __init__(self, scenario_dir: str = "/var/sil/scenarios") -> None:
        self._dir = Path(scenario_dir)
        self._dir.mkdir(parents=True, exist_ok=True)

    def list_scenarios(self) -> list[dict]:
        """Return a summary dict for every .yaml file in the scenario dir."""
        results: list[dict] = []
        for f in sorted(self._dir.glob("*.yaml")):
            results.append({
                "id": f.stem,
                "name": f.stem.replace("-", " ").title(),
                "encounter_type": "head-on",
            })
        return results

    def get_scenario(self, scenario_id: str) -> dict | None:
        """Read a single scenario by ID (stem).  Returns None if missing."""
        path = self._dir / f"{scenario_id}.yaml"
        if not path.exists():
            return None
        content = path.read_text()
        return {
            "yaml_content": content,
            "hash": hashlib.sha256(content.encode()).hexdigest(),
        }

    def create_scenario(self, yaml_content: str) -> dict:
        """Persist a new scenario file, returning its ID and content hash."""
        sid = uuid.uuid4().hex[:12]
        (self._dir / f"{sid}.yaml").write_text(yaml_content)
        return {
            "scenario_id": sid,
            "hash": hashlib.sha256(yaml_content.encode()).hexdigest(),
        }

    def delete_scenario(self, scenario_id: str) -> bool:
        """Remove a scenario file by ID.  Returns False if not found."""
        path = self._dir / f"{scenario_id}.yaml"
        if not path.exists():
            return False
        path.unlink()
        return True


def main() -> None:
    """CLI entry point (ROS2 console_scripts stub)."""
    print("ScenarioAuthoringNode ready")
