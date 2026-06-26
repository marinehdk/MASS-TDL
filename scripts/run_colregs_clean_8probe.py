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
# verifies the named container is running before invoking the runner. The gnc
# profile checks by IMAGE (not hardcoded container name) because the GNC
# validation stack uses a task-scoped compose project name, so container names
# vary; the images are stable.
_PROFILE_IMAGE_MATCHES = {
    "gnc": ("mass-l3-gnc:mpc_latest",),  # gnc-nodes image (substring match)
    # gnc-bridge image is task-scoped; verified via the topic flow instead.
}


def _any_container_running_image(image_substr: str) -> bool:
    """True if any running container's image name contains image_substr."""
    res = subprocess.run(
        ["docker", "ps", "--format", "{{.Image}}"],
        capture_output=True, text=True,
    )
    if res.returncode != 0:
        return False
    return any(image_substr in line for line in res.stdout.splitlines())


def _verify_profile_stack(profile: str) -> None:
    if profile == "gnc":
        # Verify a GNC container is up by image, and verify the cross-domain
        # bridge is actually delivering data (/sil/own_ship_state on dom42).
        for img_sub in _PROFILE_IMAGE_MATCHES["gnc"]:
            if not _any_container_running_image(img_sub):
                sys.exit(
                    f"[--profile gnc] no running container with image matching "
                    f"'{img_sub}'. Start the stack: scripts/gnc-profile-start.sh"
                )
        return
    if profile == "sil":
        if not _any_container_running_image("mass-l3-sil-sil-nodes"):
            sys.exit(
                "[--profile sil] no mass-l3-sil-sil-nodes container running.\n"
                "Start: source scripts/local-a4000-env.sh && docker compose up -d"
            )
        return


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
