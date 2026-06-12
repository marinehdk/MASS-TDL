from __future__ import annotations

from pathlib import Path
from typing import Any

from fastapi import APIRouter, HTTPException

from sil_orchestrator.integration.probe import probe_active_profile
from sil_orchestrator.integration.profiles import (
    AdapterState,
    IntegrationProfile,
    load_profiles,
)

router = APIRouter(prefix="/api/v1/integration")

_PROFILE_DIR = Path(__file__).resolve().parents[3] / "config" / "integration_profiles"
_profiles = load_profiles(_PROFILE_DIR)
_active_profile_name = "default"


def _profile_to_dict(profile: IntegrationProfile) -> dict[str, Any]:
    return {
        "name": profile.name,
        "mode": profile.mode,
        "tdl_domain_id": profile.tdl_domain_id,
        "adapters": {
            name: adapter.state.value for name, adapter in profile.adapters.items()
        },
        "external_domains": {
            name: {
                "domain_id": domain.domain_id,
                "workspace_setup": domain.workspace_setup,
                "required_topics": dict(domain.required_topics),
            }
            for name, domain in profile.external_domains.items()
        },
        "freshness": {
            "ownship_ms": profile.freshness.ownship_ms,
            "targets_ms": profile.freshness.targets_ms,
            "environment_ms": profile.freshness.environment_ms,
        },
        "safety": {
            "route_out_requires_screen02_pass": (
                profile.safety.route_out_requires_screen02_pass
            ),
            "forbid_low_level_control": profile.safety.forbid_low_level_control,
        },
    }


def _active_profile() -> IntegrationProfile:
    return _profiles[_active_profile_name]


@router.get("/profiles")
async def list_integration_profiles() -> dict[str, object]:
    return {
        "active_profile": _active_profile_name,
        "profiles": sorted(_profiles),
    }


@router.get("/profile")
async def get_integration_profile() -> dict[str, Any]:
    return _profile_to_dict(_active_profile())


@router.post("/profile")
async def select_integration_profile(request: dict[str, str]) -> dict[str, Any]:
    global _active_profile_name
    name = request.get("name", "")
    if name not in _profiles:
        raise HTTPException(status_code=404, detail="Integration profile not found")
    _active_profile_name = name
    return _profile_to_dict(_active_profile())


@router.get("/status")
async def get_integration_status() -> dict[str, object]:
    profile = _active_profile()
    route_out_enabled = profile.adapters["route_out"].state is AdapterState.ENABLED
    return {
        "active_profile": profile.name,
        "external_enabled": profile.mode == "external",
        "route_out_enabled": route_out_enabled,
    }


@router.post("/probe")
async def probe_integration_profile() -> dict[str, object]:
    return probe_active_profile(_active_profile()).to_dict()
