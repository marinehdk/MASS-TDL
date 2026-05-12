"""File-backed scenario store. Reads/writes YAML under SCENARIO_DIR."""

import hashlib
import uuid
from pathlib import Path

from sil_orchestrator.config import SCENARIO_DIR


class ScenarioStore:
    def __init__(self, base_dir: Path | None = None) -> None:
        self._dir = base_dir or SCENARIO_DIR
        self._dir_ready = False

    def _ensure_dir(self) -> None:
        if not self._dir_ready:
            self._dir.mkdir(parents=True, exist_ok=True)
            self._dir_ready = True

    def list(self) -> list[dict]:
        self._ensure_dir()
        results = []
        for f in sorted(self._dir.glob("*.yaml")):
            results.append({
                "id": f.stem,
                "name": f.stem.replace("-", " ").title(),
                "encounter_type": "head-on",
            })
        return results

    def get(self, scenario_id: str) -> dict | None:
        path = self._dir / f"{scenario_id}.yaml"
        if not path.exists():
            return None
        content = path.read_text()
        h = hashlib.sha256(content.encode()).hexdigest()
        return {"yaml_content": content, "hash": h}

    def create(self, yaml_content: str) -> dict:
        self._ensure_dir()
        sid = uuid.uuid4().hex[:12]
        path = self._dir / f"{sid}.yaml"
        path.write_text(yaml_content)
        h = hashlib.sha256(yaml_content.encode()).hexdigest()
        return {"scenario_id": sid, "hash": h}

    def update(self, scenario_id: str, yaml_content: str) -> dict | None:
        path = self._dir / f"{scenario_id}.yaml"
        if not path.exists():
            return None
        path.write_text(yaml_content)
        h = hashlib.sha256(yaml_content.encode()).hexdigest()
        return {"hash": h}

    def delete(self, scenario_id: str) -> bool:
        self._ensure_dir()
        path = self._dir / f"{scenario_id}.yaml"
        if not path.exists():
            return False
        path.unlink()
        return True

    def validate(self, yaml_content: str) -> dict:
        errors = []
        if not yaml_content.strip():
            errors.append("YAML content is empty")
        # Phase 2: add cerberus-cpp / pyyaml schema validation
        return {"valid": len(errors) == 0, "errors": errors}
