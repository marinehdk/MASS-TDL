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
}


def _any_container_running_image(image_substr: str) -> bool:
    """True if any running container's image name contains image_substr."""
    res = subprocess.run(
        ["docker", "ps", "--format", "{{.Image}}"],
        capture_output=True,
        text=True,
    )
    if res.returncode != 0:
        return False
    return any(image_substr in line for line in res.stdout.splitlines())


def _verify_profile_stack(profile: str) -> None:
    if profile == "gnc":
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
        description="Run COLREGs clean 8-probe scenarios.",
        add_help=False,
    )
    parser.add_argument(
        "--profile",
        choices=("sil", "gnc"),
        default="sil",
        help="Execution stack target (sil=default, gnc=GNC integration).",
    )
    parser.add_argument(
        "--m5-short-avoidance-gate",
        action="store_true",
        help=(
            "Cap the sim horizon at 900s so two complete 90s M5 optimized-"
            "avoidance cadence windows can be judged without running the full "
            "encounter+return lifecycle. Forwards --total-time-override 900.0 "
            "to the underlying runner unless the caller supplied a smaller one."
        ),
    )
    known, remaining = parser.parse_known_args(argv)
    if "--list" in remaining:
        return _load_runner().main(remaining)
    _verify_profile_stack(known.profile)
    # Forward the resolved profile into the runner so it can apply profile-aware
    # behaviour (e.g. the gnc three-container restart set) without re-parsing.
    remaining = ["--profile", known.profile, *remaining]
    # FAST gate: cap the sim horizon at 900s so the M5 optimized-avoidance
    # cadence can be judged quickly without running the full encounter+return
    # lifecycle. If the caller already supplied --total-time-override, honor
    # the smaller of the two; otherwise inject 900.0.
    if known.m5_short_avoidance_gate:
        if "--total-time-override" in remaining:
            idx = remaining.index("--total-time-override")
            try:
                existing = float(remaining[idx + 1])
                remaining[idx + 1] = str(min(existing, 900.0))
            except (IndexError, ValueError):
                pass
        else:
            remaining.extend(["--total-time-override", "900.0"])
    return _load_runner().main(remaining)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
