"""Cerberus validator for FCB scenario YAML files (maritime-schema v3.0)."""

from pathlib import Path
from typing import Any, Dict

import yaml
from cerberus import Validator


def _load_schema() -> Dict[str, Any]:
    schema_path = Path(__file__).parent / "cerberus_schema" / "fcb_scenario_v3.yaml"
    with open(schema_path, "r") as f:
        return yaml.safe_load(f)


_SCHEMA = _load_schema()


def validate_yaml(data: Dict[str, Any]) -> None:
    """Validate parsed YAML data against the FCB scenario schema.

    Args:
        data: Parsed YAML content as a Python dict.

    Raises:
        ValueError: If validation fails, with details of the errors.
    """
    v = Validator(_SCHEMA, allow_unknown=True)
    if not v.validate(data):
        raise ValueError(f"Scenario validation failed: {v.errors}")


def validate_file(path: Path) -> None:
    """Load and validate a YAML file against the FCB scenario schema.

    Args:
        path: Path to the YAML file.

    Raises:
        ValueError: If validation fails.
        FileNotFoundError: If the file does not exist.
        yaml.YAMLError: If the file is not valid YAML.
    """
    with open(path, "r") as f:
        data = yaml.safe_load(f)
    validate_yaml(data)
