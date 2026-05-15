"""File-backed scenario store. Reads/writes YAML under SCENARIO_DIR.

Recursively scans for ``*.yaml`` so the layout can have an ``imazu22/`` subdir,
and infers the COLREGs encounter type from filename suffix tokens.
"""

import hashlib
import json
import re
import uuid
from pathlib import Path

import yaml

from sil_orchestrator.config import SCENARIO_DIR

_ENC_TOKEN_MAP = {
    "ho": "head-on",
    "head_on": "head-on",
    "head-on": "head-on",
    "headon": "head-on",
    "cr-gw": "crossing-giveway",
    "cr-so": "crossing-standon",
    "cr": "crossing",
    "cs": "crossing",          # rule15 crossing situation
    "crossing": "crossing",
    "ot": "overtaking",
    "overtaking": "overtaking",
    "ms": "multi-ship",
    "multiship": "multi-ship",
}


def _infer_encounter_type(stem: str) -> str:
    s = stem.lower()
    # Pass 1: exact split-token match (handles imazu-NN-ho-v1.0)
    for tok in re.split(r"[-_]", s):
        if tok in _ENC_TOKEN_MAP:
            return _ENC_TOKEN_MAP[tok]
    # Pass 2: substring (handles r14_head_on, custom names with full phrases)
    for key, value in _ENC_TOKEN_MAP.items():
        if len(key) >= 3 and key in s:
            return value
    return "unspecified"


_SCHEMA_PATH = Path(__file__).resolve().parent.parent.parent / "scenarios" / "schema" / "fcb_traffic_situation.schema.json"


class ScenarioStore:
    def __init__(self, base_dir: Path | None = None) -> None:
        self._dir = base_dir or SCENARIO_DIR
        self._dir_ready = False

    def _ensure_dir(self) -> None:
        if not self._dir_ready:
            self._dir.mkdir(parents=True, exist_ok=True)
            self._dir_ready = True

    def _path_for(self, scenario_id: str) -> Path | None:
        # First try flat layout
        flat = self._dir / f"{scenario_id}.yaml"
        if flat.exists():
            return flat
        # Fallback: recursive search by stem
        for f in self._dir.rglob(f"{scenario_id}.yaml"):
            return f
        return None

    def list(self) -> list[dict]:
        self._ensure_dir()
        results = []
        for f in sorted(self._dir.rglob("*.yaml")):
            # Skip schemas or non-scenario files
            if f.name == "schema.yaml":
                continue
            # folder is relative to base dir
            try:
                folder = f.relative_to(self._dir).parent.name
            except ValueError:
                folder = f.parent.name
            
            results.append({
                "id": f.stem,
                "name": f.stem.replace("-", " ").replace("_", " ").title(),
                "encounter_type": _infer_encounter_type(f.stem),
                "folder": folder or "root"
            })
        return results

    def get(self, scenario_id: str) -> dict | None:
        path = self._path_for(scenario_id)
        if path is None:
            return None
        content = path.read_text()
        h = hashlib.sha256(content.encode()).hexdigest()
        backend: str = "demo"
        try:
            yaml_data = yaml.safe_load(content)
            if isinstance(yaml_data, dict):
                metadata = yaml_data.get("metadata", {})
                if isinstance(metadata, dict):
                    sim_settings = metadata.get("simulation_settings", {})
                    if isinstance(sim_settings, dict):
                        backend = sim_settings.get("backend", "demo")
        except (yaml.YAMLError, AttributeError):
            pass
        return {"yaml_content": content, "hash": h, "backend": backend}

    def create(self, yaml_content: str) -> dict:
        self._ensure_dir()
        sid = uuid.uuid4().hex[:12]
        path = self._dir / f"{sid}.yaml"
        path.write_text(yaml_content)
        h = hashlib.sha256(yaml_content.encode()).hexdigest()
        return {"scenario_id": sid, "hash": h}

    def update(self, scenario_id: str, yaml_content: str) -> dict | None:
        path = self._path_for(scenario_id)
        if path is None:
            return None
        path.write_text(yaml_content)
        h = hashlib.sha256(yaml_content.encode()).hexdigest()
        return {"hash": h}

    def delete(self, scenario_id: str) -> bool:
        path = self._path_for(scenario_id)
        if path is None:
            return False
        path.unlink()
        return True

    def validate(self, yaml_content: str) -> dict:
        """Validate YAML content against fcb_traffic_situation.schema.json (JSON Schema Draft-07).

        Returns {"valid": bool, "errors": [str], "schema_version": str}.
        """
        errors = []
        schema_version = "unknown"

        if not yaml_content.strip():
            return {"valid": False, "errors": ["YAML content is empty"], "schema_version": "unknown"}

        try:
            doc = yaml.safe_load(yaml_content)
        except yaml.YAMLError as e:
            return {"valid": False, "errors": [f"YAML parse error: {e}"], "schema_version": "unknown"}

        if doc is None:
            return {"valid": False, "errors": ["YAML parsed to None (empty document)"], "schema_version": "unknown"}

        if isinstance(doc, dict):
            schema_version = doc.get("metadata", {}).get("schema_version", "unknown")

        try:
            import jsonschema
        except ImportError:
            return {"valid": True, "errors": ["jsonschema not installed — schema validation skipped"], "schema_version": schema_version}

        try:
            with open(_SCHEMA_PATH) as f:
                schema = json.load(f)
            validator = jsonschema.Draft7Validator(schema)
            for err in validator.iter_errors(doc):
                path = ".".join(str(p) for p in err.absolute_path) if err.absolute_path else "(root)"
                errors.append(f"{path}: {err.message}")
        except (json.JSONDecodeError, FileNotFoundError) as e:
            errors.append(f"Schema loading error: {e}")

        return {"valid": len(errors) == 0, "errors": errors, "schema_version": schema_version}
