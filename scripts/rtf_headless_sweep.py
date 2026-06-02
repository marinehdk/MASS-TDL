#!/usr/bin/env python3
"""Headless RTF envelope sweep — no browser connected (pure backend path).

For each nominal rate, drives the orchestrator REST lifecycle, then regresses
own_ship sim_t vs wall_t (least-squares slope) to recover the TRUE real-time
factor the backend can sustain without foxglove WebSocket / HMI contention.

Usage: python3 scripts/rtf_headless_sweep.py [scenario] [rates...]
"""
import json, os, ssl, sys, time, urllib.request

BASE = os.environ.get("ORCH_URL", "https://127.0.0.1:8000") + "/api/v1"
CTX = ssl.create_default_context()
CTX.check_hostname = False
CTX.verify_mode = ssl.CERT_NONE

SCENARIO = sys.argv[1] if len(sys.argv) > 1 else "colreg-rule14-ho"
RATES = [float(x) for x in sys.argv[2:]] or [5.0, 10.0, 20.0, 50.0]
SETTLE_S = float(os.environ.get("SETTLE_S", 4.0))   # drop rate-switch catch-up transient
SAMPLE_S = float(os.environ.get("SAMPLE_S", 25.0))  # regression window (wall seconds)
INTERVAL = float(os.environ.get("INTERVAL", 1.0))


def req(method, path, body=None, timeout=30, retries=3):
    data = json.dumps(body).encode() if body is not None else None
    last = None
    for attempt in range(retries):
        try:
            r = urllib.request.Request(BASE + path, data=data, method=method,
                                       headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(r, context=CTX, timeout=timeout) as resp:
                return json.loads(resp.read().decode())
        except Exception as exc:  # noqa: BLE001
            last = exc
            time.sleep(2.0)
    raise last


def oss_sample():
    body = req("GET", "/debug/snapshot")
    oss = body.get("topics", {}).get("/sil/own_ship_state")
    if not oss:
        return None
    return float(oss["wall_t"]), float(oss["sim_t"])


def slope(xs, ys):
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    num = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    den = sum((x - mx) ** 2 for x in xs)
    return num / den if den else float("nan")


def run_rate(rate):
    req("POST", "/lifecycle/cleanup")
    time.sleep(3.0)
    c = req("POST", "/lifecycle/configure", {"scenario_id": SCENARIO})
    if not c.get("success"):
        print(f"  configure FAILED: {c.get('error')}")
        return rate, float("nan"), 0
    time.sleep(2.0)
    a = req("POST", "/lifecycle/activate")
    if not a.get("success"):
        print(f"  activate FAILED: {a.get('error')}")
        return rate, float("nan"), 0
    # wait for ACTIVE before touching rate / sampling
    for _ in range(15):
        if req("GET", "/lifecycle/status").get("current_state") == "active":
            break
        time.sleep(1)
    req("POST", "/lifecycle/rate", {"rate": rate})
    time.sleep(SETTLE_S)
    walls, sims = [], []
    t0 = time.time()
    while time.time() - t0 < SAMPLE_S:
        s = oss_sample()
        if s:
            walls.append(s[0]); sims.append(s[1])
        time.sleep(INTERVAL)
    if len(walls) < 5:
        return rate, float("nan"), len(walls)
    return rate, slope(walls, sims), len(walls)


def main():
    print(f"scenario={SCENARIO}  rates={RATES}  (settle={SETTLE_S}s window={SAMPLE_S}s)\n")
    results = []
    for r in RATES:
        rate, rtf, n = run_rate(r)
        eff = rtf / rate if rate else float("nan")
        print(f"nominal {rate:>4.0f}x  ->  measured RTF = {rtf:5.2f}x  "
              f"(efficiency {eff*100:4.0f}%, {n} samples)")
        results.append((rate, rtf, eff))
    req("POST", "/lifecycle/cleanup")
    print("\nsummary (headless, no browser):")
    for rate, rtf, eff in results:
        print(f"  {rate:>4.0f}x nominal -> {rtf:5.2f}x real  ({eff*100:3.0f}%)")


if __name__ == "__main__":
    main()
