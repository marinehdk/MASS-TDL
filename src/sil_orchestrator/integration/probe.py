from __future__ import annotations

import os
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from sil_orchestrator.integration.profiles import AdapterState, IntegrationProfile


@dataclass(frozen=True)
class ProbeCheck:
    gate_id: int
    label: str
    passed: bool
    detail: str

    def to_dict(self) -> dict[str, object]:
        return {
            "gate_id": self.gate_id,
            "label": self.label,
            "passed": self.passed,
            "detail": self.detail,
        }


@dataclass(frozen=True)
class ProbeReport:
    profile_name: str
    all_clear: bool
    checks: list[ProbeCheck]

    def to_dict(self) -> dict[str, object]:
        return {
            "profile_name": self.profile_name,
            "all_clear": self.all_clear,
            "checks": [check.to_dict() for check in self.checks],
        }


Runner = Callable[[list[str], dict[str, str], float], subprocess.CompletedProcess[str]]


def run_command(
    command: list[str], env: dict[str, str], timeout_s: float
) -> subprocess.CompletedProcess[str]:
    merged_env = os.environ.copy()
    merged_env.update(env)
    return subprocess.run(
        command,
        capture_output=True,
        env=merged_env,
        text=True,
        timeout=timeout_s,
    )


def _topic_info(
    topic: str, domain_id: int, runner: Runner
) -> subprocess.CompletedProcess[str]:
    return runner(
        ["ros2", "topic", "info", topic],
        {"ROS_DOMAIN_ID": str(domain_id)},
        3.0,
    )


def probe_active_profile(
    profile: IntegrationProfile, runner: Runner = run_command
) -> ProbeReport:
    checks = [ProbeCheck(1, "Profile valid", True, f"profile {profile.name} loaded")]

    if profile.mode == "default":
        disabled = all(
            adapter.state is AdapterState.DISABLED for adapter in profile.adapters.values()
        )
        checks.append(
            ProbeCheck(
                2,
                "External adapters disabled",
                disabled,
                "all adapters disabled" if disabled else "one or more adapters enabled",
            )
        )
        return ProbeReport(profile.name, all(check.passed for check in checks), checks)

    gate_id = 2
    for domain_name, domain in profile.external_domains.items():
        workspace_exists = Path(domain.workspace_setup).exists()
        checks.append(
            ProbeCheck(
                gate_id,
                f"{domain_name} workspace setup",
                workspace_exists,
                domain.workspace_setup,
            )
        )
        gate_id += 1

        for topic, expected_type in domain.required_topics.items():
            try:
                result = _topic_info(topic, domain.domain_id, runner)
            except FileNotFoundError as exc:
                result = None
                type_matches = False
                output = f"FileNotFoundError: missing executable {exc}"
            except subprocess.TimeoutExpired as exc:
                result = None
                type_matches = False
                output = f"timeout after {exc.timeout}s: {' '.join(exc.cmd)}"
            else:
                output = "\n".join([result.stdout or "", result.stderr or ""]).strip()
                type_matches = result.returncode == 0 and (
                    f"Type: {expected_type}" in (result.stdout or "")
                )
            checks.append(
                ProbeCheck(
                    gate_id,
                    f"{domain_name} topic {topic}",
                    type_matches,
                    output or f"returncode={result.returncode if result else 'error'}",
                )
            )
            gate_id += 1

    route_out_enabled = (
        profile.adapters["route_out"].state is AdapterState.ENABLED
    )
    low_level_forbidden = (
        route_out_enabled and profile.safety.forbid_low_level_control
    )
    checks.append(
        ProbeCheck(
            gate_id,
            "Low-level control forbidden",
            low_level_forbidden,
            "route_out enabled and low-level control forbidden"
            if low_level_forbidden
            else "route_out must be enabled and low-level control must be forbidden",
        )
    )

    return ProbeReport(profile.name, all(check.passed for check in checks), checks)
