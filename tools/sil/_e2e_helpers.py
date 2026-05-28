"""DEMO-1 e2e test helpers — REST + ROS2 topic sampling utilities.

Used by test_demo1_imazu01ho_e2e.py for A-1 ~ A-9 + V1-V3 assertions.
"""
from __future__ import annotations
import json
import subprocess
import time
import urllib.request
import ssl

BASE = "https://localhost:8000/api/v1"
CONTAINER = "mass-l3-tacticallayer-sil-nodes-1"
_SSL_CTX = ssl._create_unverified_context()


def _get(path: str) -> dict:
    with urllib.request.urlopen(f"{BASE}{path}", context=_SSL_CTX, timeout=10) as r:
        return json.loads(r.read())


def _post(path: str, body: dict | None = None) -> dict:
    data = json.dumps(body or {}).encode()
    req = urllib.request.Request(
        f"{BASE}{path}", data=data,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, context=_SSL_CTX, timeout=15) as r:
        resp_body = r.read()
        return json.loads(resp_body) if resp_body else {}


def _topic_echo_once(topic: str, timeout_s: int = 5) -> dict | None:
    """ros2 topic echo --once via docker exec, return parsed as dict (best effort)."""
    cmd = [
        "docker", "exec", CONTAINER, "bash", "-c",
        f"source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash "
        f"&& timeout {timeout_s} ros2 topic echo {topic} --once",
    ]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s + 5)
    if r.returncode != 0 or not r.stdout:
        return None
    try:
        import yaml
        return yaml.safe_load(r.stdout)
    except Exception:
        return _parse_kv(r.stdout)


def _parse_kv(text: str) -> dict:
    """Fallback parse: simple key:value lines."""
    out = {}
    for line in text.splitlines():
        if ":" in line and not line.strip().startswith("#"):
            k, _, v = line.strip().partition(":")
            try:
                out[k.strip()] = float(v.strip())
            except ValueError:
                out[k.strip()] = v.strip()
    return out


def _collect_topic(
    topic: str, from_sim_t: float, to_sim_t: float, n: int = 10,
) -> list[dict]:
    """Sample `n` messages spaced across [from_sim_t, to_sim_t] sim-time window."""
    samples: list[dict] = []
    interval = (to_sim_t - from_sim_t) / max(1, n)
    while True:
        st = _get("/lifecycle/status")
        if st.get("sim_time_s", 0) >= from_sim_t:
            break
        if st.get("current_state") != "active":
            return []
        time.sleep(1)
    for i in range(n):
        target_t = from_sim_t + interval * i
        for _ in range(120):
            st = _get("/lifecycle/status")
            if st.get("sim_time_s", 0) >= target_t:
                break
            if st.get("current_state") != "active":
                return samples
            time.sleep(0.5)
        msg = _topic_echo_once(topic)
        if msg is not None:
            samples.append(msg)
    return samples


def _wait_until_sim_t(target_s: float, timeout_wall_s: float = 1200.0):
    """Block until /lifecycle/status.sim_time_s >= target_s, or timeout."""
    start = time.time()
    while time.time() - start < timeout_wall_s:
        st = _get("/lifecycle/status")
        if st.get("sim_time_s", 0) >= target_s:
            return
        if st.get("current_state") == "inactive":
            return
        time.sleep(2)
    raise TimeoutError(f"sim_time never reached {target_s}s within {timeout_wall_s}s wall")
