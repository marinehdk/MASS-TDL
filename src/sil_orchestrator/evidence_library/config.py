from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class EvidenceRootConfig:
    root_id: str
    label: str
    source: str
    path_glob: str
    enabled: bool = True
    trusted: bool = False
    allow_retention_mutation: bool = False
    follow_symlinks: bool = False


@dataclass(frozen=True)
class EvidenceLibraryConfig:
    config_home: Path
    database_path: Path
    roots: list[EvidenceRootConfig]
    raw_trace_policy: str
    effective_retention_policy: str


def _repo_root(repo_root: Path | None) -> Path:
    if repo_root is not None:
        return repo_root.resolve()
    return Path(__file__).resolve().parents[3]


def _config_home() -> Path:
    explicit = os.getenv("MASS_L3_CONFIG_HOME")
    if explicit:
        return Path(explicit).expanduser()
    return Path.home() / ".config" / "mass-l3"


def _default_config(repo_root: Path) -> dict[str, Any]:
    return {
        "database_path": "{config_home}/evidence_index.sqlite",
        "raw_trace_policy": "compress_after_ingest",
        "roots": [
            {
                "root_id": "primary-unified",
                "label": "Primary checkout unified runs",
                "source": "background_probe",
                "path_glob": "{repo_root}/runs/*/trace",
                "enabled": True,
                "trusted": True,
                "allow_retention_mutation": False,
                "follow_symlinks": False,
            },
            {
                "root_id": "worktrees-unified",
                "label": "Worktree unified runs",
                "source": "background_probe",
                "path_glob": "{repo_root}/.worktrees/*/runs/*/trace",
                "enabled": True,
                "trusted": True,
                "allow_retention_mutation": False,
                "follow_symlinks": False,
            },
            {
                "root_id": "primary",
                "label": "Primary checkout trace_eval",
                "source": "background_probe",
                "path_glob": "{repo_root}/runs/trace_eval",
                "enabled": True,
                "trusted": True,
                "allow_retention_mutation": False,
                "follow_symlinks": False,
            },
            {
                "root_id": "worktrees",
                "label": "Worktree trace_eval folders",
                "source": "background_probe",
                "path_glob": "{repo_root}/.worktrees/*/runs/trace_eval",
                "enabled": True,
                "trusted": True,
                "allow_retention_mutation": False,
                "follow_symlinks": False,
            },
        ],
    }


def _merge_machine_override(base: dict[str, Any], machine_path: Path) -> dict[str, Any]:
    if not machine_path.exists():
        return base
    raw = json.loads(machine_path.read_text())
    merged = dict(base)
    for key, value in raw.items():
        if key == "roots":
            merged["roots"] = value
        else:
            merged[key] = value
    return merged


def _expand(value: str, *, repo_root: Path, config_home: Path) -> str:
    return (
        value.replace("{repo_root}", str(repo_root))
        .replace("{config_home}", str(config_home))
    )


def load_effective_config(repo_root: Path | None = None) -> EvidenceLibraryConfig:
    repo = _repo_root(repo_root)
    home = _config_home()
    base = _default_config(repo)
    merged = _merge_machine_override(base, home / "evidence_library.json")
    database_path = Path(
        _expand(
            str(merged["database_path"]),
            repo_root=repo,
            config_home=home,
        )
    ).expanduser()
    roots = [
        EvidenceRootConfig(
            root_id=str(item["root_id"]),
            label=str(item.get("label") or item["root_id"]),
            source=str(item.get("source") or "background_probe"),
            path_glob=_expand(
                str(item["path_glob"]),
                repo_root=repo,
                config_home=home,
            ),
            enabled=bool(item.get("enabled", True)),
            trusted=bool(item.get("trusted", False)),
            allow_retention_mutation=bool(item.get("allow_retention_mutation", False)),
            follow_symlinks=bool(item.get("follow_symlinks", False)),
        )
        for item in merged.get("roots", [])
    ]
    raw_trace_policy = str(merged.get("raw_trace_policy", "compress_after_ingest"))
    return EvidenceLibraryConfig(
        config_home=home,
        database_path=database_path,
        roots=roots,
        raw_trace_policy=raw_trace_policy,
        effective_retention_policy="keep",
    )
