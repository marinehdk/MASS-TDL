#!/usr/bin/env python3
"""M4/M5 stub acceptance test — DoD #8 #9.
Usage: python tools/vv/stub_acceptance.py --stub m4
       python tools/vv/stub_acceptance.py --stub m5
"""
from __future__ import annotations
import argparse, json, subprocess, sys, time
from pathlib import Path

def echo_once(topic: str, timeout: float = 10.0) -> dict | None:
    try:
        result = subprocess.run(
            ["ros2", "topic", "echo", topic, "--once", "--spin-time", str(timeout)],
            capture_output=True, text=True, timeout=timeout + 5)
        if result.returncode != 0 or not result.stdout.strip():
            return None
        import yaml
        return yaml.safe_load(result.stdout)
    except (FileNotFoundError, subprocess.TimeoutExpired, Exception):
        return None

def check_m4_stub() -> dict:
    msg = echo_once("/sil/sat2_data")
    if msg is None:
        return {"pass": False, "reason": "No message from /sil/sat2_data within 10s"}
    contribs = msg.get("ivp_contributions", [])
    if len(contribs) != 8:
        return {"pass": False, "reason": f"Expected 8 contributions, got {len(contribs)}"}
    if all(abs(c.get("cost", 0)) < 1e-6 for c in contribs):
        return {"pass": False, "reason": "All costs zero"}
    maturity = msg.get("algorithm_maturity", {}).get("m4_ivp", "unknown")
    return {"pass": True, "ivp_count": len(contribs), "algorithm_maturity": maturity,
            "sample_costs": [c.get("cost") for c in contribs]}

def check_m5_stub() -> dict:
    msg = echo_once("/sil/sat3_data")
    if msg is None:
        return {"pass": False, "reason": "No message from /sil/sat3_data within 10s"}
    candidates = msg.get("trajectory_candidates", [])
    if len(candidates) < 1:
        return {"pass": False, "reason": f"Expected >=1 candidates, got {len(candidates)}"}
    valid_types = {"mid_mpc", "bc_mpc"}
    for c in candidates:
        if c.get("type", "") not in valid_types:
            return {"pass": False, "reason": f"Invalid type: {c.get('type')}"}
    maturity = msg.get("algorithm_maturity", {}).get("m5_bc_mpc", "unknown")
    return {"pass": True, "candidates_count": len(candidates), "algorithm_maturity": maturity}

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--stub", choices=["m4","m5","both"], default="both")
    ap.add_argument("--output", default="test-results/stub_acceptance.json")
    args = ap.parse_args()
    results: dict = {}
    if args.stub in ("m4","both"):
        results["m4"] = check_m4_stub()
        ok = results["m4"]["pass"]
        print(f"[DoD #8] M4 stub: {'PASS' if ok else 'FAIL'} — {results['m4'].get('reason','')}")
    if args.stub in ("m5","both"):
        results["m5"] = check_m5_stub()
        ok = results["m5"]["pass"]
        print(f"[DoD #9] M5 stub: {'PASS' if ok else 'FAIL'} — {results['m5'].get('reason','')}")
    out = Path(args.output); out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(results, indent=2))
    return 0 if all(v.get("pass",False) for v in results.values()) else 1

if __name__ == "__main__":
    sys.exit(main())
