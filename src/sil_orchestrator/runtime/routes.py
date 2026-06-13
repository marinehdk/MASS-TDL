from __future__ import annotations

import os
from collections.abc import Callable
from functools import lru_cache
from pathlib import Path
from typing import TypeVar

from fastapi import APIRouter, Depends, HTTPException
from yaml import YAMLError

from sil_orchestrator.runtime.compose import ComposeRuntime, ComposeRuntimeError
from sil_orchestrator.runtime.manifests import (
    RuntimeManifestError,
    load_plugin_manifests,
    load_runtime_profiles,
)
from sil_orchestrator.runtime.service import RuntimeConsoleService


router = APIRouter(prefix="/api/v1/runtime")

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_PLUGIN_DIR = _PROJECT_ROOT / "config" / "runtime_plugins"
_PROFILE_DIR = _PROJECT_ROOT / "config" / "runtime_profiles"
RuntimeResult = TypeVar("RuntimeResult")


def _compose_files() -> tuple[str, ...]:
    value = os.environ.get("COMPOSE_FILE", "docker-compose.yml")
    return tuple(part for part in value.split(":") if part) or ("docker-compose.yml",)


def _runs_dir() -> Path:
    value = os.environ.get("SIL_RUN_DIR")
    if not value:
        return _PROJECT_ROOT / "runs"
    path = Path(value)
    return path if path.is_absolute() else _PROJECT_ROOT / path


@lru_cache(maxsize=1)
def get_runtime_service() -> RuntimeConsoleService:
    try:
        plugins = load_plugin_manifests(_PLUGIN_DIR)
        profiles = load_runtime_profiles(_PROFILE_DIR, plugins)
    except (RuntimeManifestError, OSError, YAMLError) as exc:
        raise HTTPException(
            status_code=503,
            detail=f"invalid runtime configuration: {exc}",
        ) from exc

    active_profile_name = os.environ.get("TDL_RUNTIME_PROFILE", "integration-local")
    if active_profile_name not in profiles:
        available = ", ".join(sorted(profiles)) or "none"
        raise HTTPException(
            status_code=400,
            detail=(
                f"unknown TDL_RUNTIME_PROFILE {active_profile_name!r}; "
                f"available profiles: {available}"
            ),
        )

    compose = ComposeRuntime(
        compose_files=_compose_files(),
        project_name=os.environ.get("COMPOSE_PROJECT_NAME", "mass-l3-sil"),
    )
    return RuntimeConsoleService(
        plugins=plugins,
        profiles=profiles,
        compose=compose,
        runs_dir=_runs_dir(),
        active_profile_name=active_profile_name,
    )


def _require_accepted(
    result: dict[str, object],
    status_code: int = 400,
) -> dict[str, object]:
    if not result.get("accepted"):
        raise HTTPException(
            status_code=status_code,
            detail=str(result.get("error", "runtime action rejected")),
        )
    return result


def _run_runtime(action: Callable[[], RuntimeResult]) -> RuntimeResult:
    try:
        return action()
    except ComposeRuntimeError as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc


@router.get("/summary")
async def runtime_summary(
    service: RuntimeConsoleService = Depends(get_runtime_service),
) -> dict[str, object]:
    return _run_runtime(service.summary)


@router.get("/core-services")
async def runtime_core_services(
    service: RuntimeConsoleService = Depends(get_runtime_service),
) -> dict[str, object]:
    return {"services": _run_runtime(service.core_services)}


@router.get("/plugins")
async def runtime_plugins(
    service: RuntimeConsoleService = Depends(get_runtime_service),
) -> dict[str, object]:
    return {"roles": _run_runtime(service.plugin_roles)}


@router.post("/core/{service_id}/restart")
async def restart_core_service(
    service_id: str,
    service: RuntimeConsoleService = Depends(get_runtime_service),
) -> dict[str, object]:
    return _require_accepted(
        _run_runtime(lambda: service.restart_core_service(service_id)),
        status_code=404,
    )


@router.post("/core/start")
async def start_core_stack(
    service: RuntimeConsoleService = Depends(get_runtime_service),
) -> dict[str, object]:
    return _run_runtime(service.start_core_stack)


@router.post("/core/restart")
async def restart_core_stack(
    service: RuntimeConsoleService = Depends(get_runtime_service),
) -> dict[str, object]:
    return _run_runtime(service.restart_core_stack)


@router.post("/core/stop")
async def stop_core_stack(
    request: dict[str, str],
    service: RuntimeConsoleService = Depends(get_runtime_service),
) -> dict[str, object]:
    return _require_accepted(
        _run_runtime(lambda: service.stop_core_stack(request.get("confirm", "")))
    )


@router.post("/plugins/{role}/switch")
async def switch_plugin(
    role: str,
    request: dict[str, str],
    service: RuntimeConsoleService = Depends(get_runtime_service),
) -> dict[str, object]:
    return _require_accepted(
        _run_runtime(lambda: service.switch_plugin(role, request.get("plugin_id", "")))
    )


@router.post("/probe")
async def runtime_probe(
    service: RuntimeConsoleService = Depends(get_runtime_service),
) -> dict[str, object]:
    return _run_runtime(service.probe)
