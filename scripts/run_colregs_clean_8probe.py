#!/usr/bin/env python3
"""Canonical CLI for the COLREGs clean 8-probe acceptance workflow.

Adds a ``--profile`` option (Track A A6) that records which execution stack is
the probe target, so the run evidence is traceable to the stack that produced
it:

  --profile gnc   GNC integration stack. Requires the GNC profile to be up
                  (``scripts/gnc-profile-start.sh``): L3 sil-nodes (domain 42)
                  + gnc-nodes + gnc-bridge (domain 50, host network). The
                  gnc_bridge carries the actuator path; sil_topic_bridge and
                  l4_guidance_adapter are NOT part of this profile.
  --profile sil   (default) SIL-only stack (legacy).

The probe itself talks to the orchestrator over HTTP (SIL_ORCH_BASE_URL); the
profile flag does not change probe behavior, it only annotates the run and
verifies the named stack's key container is up.
"""

from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
from pathlib import Path


def _load_runner():
    runner_path = Path(__file__).resolve().with_name("run_6_scenarios.py")
    spec = importlib.util.spec_from_file_location("run_6_scenarios", runner_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


# Per-profile stack expectations. The probe does not start the stack; it only
# verifies the named container is running before invoking the runner.
_PROFILE_CONTAINERS = {
    "gnc": ("codex-gnc-gnc-bridge-1", "codex-gnc-gnc-nodes-1"),
    "sil": ("mass-l3-sil-sil-nodes-1",),
}


def _container_running(name: str) -> bool:
    res = subprocess.run(
        ["docker", "inspect", "--format", "{{.State.Running}}", name],
        capture_output=True, text=True,
    )
    return res.returncode == 0 and res.stdout.strip() == "true"


def _verify_profile_stack(profile: str) -> None:
    expected = _PROFILE_CONTAINERS.get(profile)
    if expected is None:
        return
    missing = [c for c in expected if not _container_running(c)]
    if missing:
        hint = (
            "scripts/gnc-profile-start.sh" if profile == "gnc"
            else "source scripts/local-a4000-env.sh && docker compose up -d"
        )
        sys.exit(
            f"[--profile {profile}] expected container(s) not running: "
            f"{', '.join(missing)}.\nStart the stack first: {hint}"
        )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Run COLREGs clean 8-probe scenarios.", add_help=False)
    parser.add_argument("--profile", choices=("sil", "gnc"), default="sil",
                        help="Execution stack target (sil=default, gnc=GNC integration).")
    known, remaining = parser.parse_known_args(argv)
    _verify_profile_stack(known.profile)
    return _load_runner().main(remaining)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
