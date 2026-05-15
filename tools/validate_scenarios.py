"""CI gate validation script for MASS L3 scenario YAML files.

Validates all scenario YAMLs against fcb_traffic_situation.schema.json.
Exit code 0 on all-pass, 1 on any fail.
"""

import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import jsonschema
import yaml

SCHEMA_PATH = Path(__file__).resolve().parent.parent / "scenarios" / "schema" / "fcb_traffic_situation.schema.json"
SCENARIO_DIR = Path(__file__).resolve().parent.parent / "scenarios"


@dataclass
class ValidationResult:
    file: str = ""
    valid: bool = True
    errors: list[str] = field(default_factory=list)
    schema_version: str = "unknown"


def load_schema() -> dict:
    with open(SCHEMA_PATH, "r") as f:
        return json.load(f)


def validate_yaml_against_schema(
    yaml_bytes: bytes,
    schema: Optional[dict] = None,
) -> ValidationResult:
    result = ValidationResult()

    if not yaml_bytes.strip():
        result.schema_version = "empty"
        return result

    try:
        doc = yaml.safe_load(yaml_bytes)
    except yaml.YAMLError as e:
        result.valid = False
        result.errors = [f"YAML parse error: {e}"]
        return result

    if not isinstance(doc, dict):
        result.valid = False
        result.errors = ["Top-level value is not a mapping"]
        return result

    if "ship_list" in doc:
        result.schema_version = "legacy_manual"
        return result

    meta_ver = doc.get("metadata", {}).get("schema_version", "unknown")
    if isinstance(meta_ver, str):
        result.schema_version = meta_ver

    if schema is None:
        schema = load_schema()

    validator = jsonschema.Draft7Validator(schema)
    for err in validator.iter_errors(doc):
        path = ".".join(str(p) for p in err.absolute_path) if err.absolute_path else "(root)"
        result.errors.append(f"{path}: {err.message}")

    if result.errors:
        result.valid = False

    return result


def validate_all_scenarios(
    directory: Optional[Path] = None,
    schema: Optional[dict] = None,
) -> list[ValidationResult]:
    if directory is None:
        directory = SCENARIO_DIR
    directory = Path(directory)

    if schema is None:
        schema = load_schema()

    results: list[ValidationResult] = []
    for f in sorted(directory.rglob("*.yaml")):
        name = f.name

        if name == "schema.yaml":
            continue
        if name.endswith("-bak.yaml"):
            continue
        if "_manifest" in str(f):
            continue

        try:
            raw = f.read_bytes()
        except Exception as e:
            results.append(ValidationResult(file=name, valid=False, errors=[f"unreadable: {e}"]))
            continue

        result = validate_yaml_against_schema(raw, schema)
        result.file = name
        results.append(result)

    return results


def _print_human(results: list[ValidationResult]) -> None:
    total = len(results)
    passed = sum(1 for r in results if r.valid)
    failed = total - passed

    print(f"\n{'=' * 60}")
    print(f"Scenario Validation Report")
    print(f"{'=' * 60}")
    print(f"Total: {total}  |  Passed: {passed}  |  Failed: {failed}")
    print(f"{'=' * 60}\n")

    for r in results:
        status = "PASS" if r.valid else "FAIL"
        print(f"  [{status}]  {r.file}  (schema: {r.schema_version})")
        if not r.valid:
            for err in r.errors:
                print(f"         {err}")
        print()

    print(f"{'=' * 60}")
    if failed == 0:
        print(f"ALL {total} SCENARIOS PASSED")
    else:
        print(f"{failed}/{total} SCENARIOS FAILED")
    print(f"{'=' * 60}\n")


def _print_json(results: list[ValidationResult]) -> None:
    data = [
        {
            "file": r.file,
            "valid": r.valid,
            "errors": r.errors,
            "schema_version": r.schema_version,
        }
        for r in results
    ]
    print(json.dumps(data, indent=2, ensure_ascii=False))


def main() -> None:
    args = sys.argv[1:]

    if not args:
        print("Usage: python tools/validate_scenarios.py [--all | --file <path>] [--json]")
        sys.exit(1)

    json_output = "--json" in args
    args = [a for a in args if a != "--json"]

    if "--all" in args:
        results = validate_all_scenarios()
        if json_output:
            _print_json(results)
        else:
            _print_human(results)
        sys.exit(0 if all(r.valid for r in results) else 1)

    elif "--file" in args:
        idx = args.index("--file")
        if idx + 1 >= len(args):
            print("Error: --file requires a path argument")
            sys.exit(1)
        filepath = Path(args[idx + 1])
        try:
            raw = filepath.read_bytes()
        except Exception as e:
            result = ValidationResult(file=str(filepath), valid=False, errors=[f"unreadable: {e}"])
            if json_output:
                _print_json([result])
            else:
                print(f"[FAIL] {result.file}")
                for e in result.errors:
                    print(f"       {e}")
            sys.exit(1)

        result = validate_yaml_against_schema(raw)
        result.file = str(filepath)
        if json_output:
            _print_json([result])
        else:
            status = "PASS" if result.valid else "FAIL"
            print(f"[{status}] {result.file}")
            if not result.valid:
                for err in result.errors:
                    print(f"       {err}")
        sys.exit(0 if result.valid else 1)

    else:
        print(f"Unknown arguments: {' '.join(args)}")
        print("Usage: python tools/validate_scenarios.py [--all | --file <path>] [--json]")
        sys.exit(1)


if __name__ == "__main__":
    main()
