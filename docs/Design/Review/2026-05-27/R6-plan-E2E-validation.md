# DEMO-1 R6 Plan E2E — 验证与端到端验收（V1-V3 + 9 断言 e2e）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 A/B/C/D 四组 plan merge 到 main 后，依次执行 V1/V2/V3 验证项确认底座模块真跑（非 stub），然后用 9 条断言 e2e test 卡 DEMO-1 验收 ground truth。

**Architecture:** V1-V3 是"代码 vs 跑通"差距检查（grep + ros2 topic echo + 静态独立性审计），E2E 是 pytest 一键跑 imazu-01-ho 全 700s + 验 A-1~A-9 断言全 PASS。任一 V 或 A 失败 → 回 Phase 1 systematic-debugging。

**Tech Stack:** pytest / ros2 cli / docker compose / FastAPI / bash

**Worktree:** 主 worktree（不创新 worktree，因为这是验收阶段，需 main 已合并 A/B/C/D 全部 commit）

**Spec 引用:** [R6-DEMO1-full-stack-spec.md](R6-DEMO1-full-stack-spec.md) §4.5 V1-V3 + §6.1 端到端测试

**前置依赖:** **必须**等 Plan A/B/C/D 全部 merge 到 main 后才执行本 plan。任一组 plan 未 merge → 本 plan 无意义。

---

## File Structure

| 文件 | 操作 | 责任 |
|---|---|---|
| `tools/sil/test_demo1_imazu01ho_e2e.py` | Create | 9 断言端到端 pytest |
| `tools/sil/v1_m5_midmpc_check.sh` | Create | V1 M5 Mid-MPC 真跑验证脚本 |
| `tools/sil/v2_m7_independence_audit.sh` | Create | V2 M7 Doer-Checker 独立性静态审计 |
| `tools/sil/v3_m2_threats_check.sh` | Create | V3 M2 World Model 真发布 threats 验证 |
| `tools/sil/_e2e_helpers.py` | Create | _get/_post/_collect_topic 等 helper |
| `docs/Design/Review/2026-05-27/R6-e2e-results.md` | Create | 跑通后填写 verdict + 9 断言 PASS/FAIL 矩阵 |

---

## Task E1: V1 — M5 Mid-MPC 真跑主路径验证

**目标**：A/B/C/D merge 后实测 M5 不应再走 "geometric starboard fallback"，rationale 应含 "Mid-MPC" / "BC-MPC"。失败 → 开 D-DEMO1-R7。

### Step E1.1: 写 V1 验证脚本

- [ ] **Create `tools/sil/v1_m5_midmpc_check.sh`**

```bash
#!/usr/bin/env bash
# V1 — Verify M5 Mid-MPC runs on main path (not geometric fallback).
# Run AFTER Plan A/B/C/D merged to main + docker compose up + scenario activated.
set -eu
RESULT_FILE="${RESULT_FILE:-/tmp/v1_m5_midmpc_result.json}"
DURATION_S=300  # observe 300s of avoidance plans
CONTAINER="${CONTAINER:-mass-l3-tacticallayer-sil-nodes-1}"

echo "[V1] Sampling /l3/m5/avoidance_plan rationale & waypoint count for ${DURATION_S}s..."

declare -i mpc_count=0 fallback_count=0 sample_count=0 max_wp=0
end_t=$(($(date +%s) + DURATION_S))
while [ "$(date +%s)" -lt "$end_t" ]; do
  msg=$(docker exec "$CONTAINER" bash -c \
    "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout 3 ros2 topic echo /l3/m5/avoidance_plan --once" 2>/dev/null || echo "")
  if [ -z "$msg" ]; then sleep 2; continue; fi
  sample_count=$((sample_count + 1))
  rationale=$(echo "$msg" | grep -m1 "rationale:" | head -1 || echo "")
  wp_count=$(echo "$msg" | grep -c "^- schema_version:" || echo 0)
  if [ "$wp_count" -gt "$max_wp" ]; then max_wp=$wp_count; fi
  if echo "$rationale" | grep -qE "Mid.MPC|BC.MPC"; then
    mpc_count=$((mpc_count + 1))
  elif echo "$rationale" | grep -qE "geometric.*fallback|starboard fallback"; then
    fallback_count=$((fallback_count + 1))
  fi
  sleep 5
done

cat > "$RESULT_FILE" <<EOF
{
  "samples": ${sample_count},
  "mpc_count": ${mpc_count},
  "fallback_count": ${fallback_count},
  "max_waypoints": ${max_wp},
  "verdict": "$([ ${mpc_count} -ge $((sample_count * 7 / 10)) ] && echo PASS || echo FAIL)"
}
EOF
cat "$RESULT_FILE"

if [ "${mpc_count}" -ge $((sample_count * 7 / 10)) ] && [ "${max_wp}" -ge 5 ]; then
  echo "[V1] PASS: M5 Mid-MPC/BC-MPC runs >=70% of samples, max waypoints=${max_wp}"
  exit 0
else
  echo "[V1] FAIL: only ${mpc_count}/${sample_count} samples on MPC path, max_wp=${max_wp} (<5)"
  exit 1
fi
```

- [ ] **chmod + run baseline**

```bash
chmod +x tools/sil/v1_m5_midmpc_check.sh
# Run during active imazu-01-ho session:
npm run sys:start
sleep 60
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/configure -H "Content-Type: application/json" -d '{"scenario_id":"imazu-01-ho"}'
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate
bash tools/sil/v1_m5_midmpc_check.sh
```

Expected if A merged: `verdict: PASS, mpc_count >= 70% samples, max_waypoints >= 5`

### Step E1.2: 写 pytest wrapper（CI 集成）

- [ ] **Add to `tools/sil/test_demo1_imazu01ho_e2e.py`（与 Task E4 合并到同一文件）**

```python
def test_V1_m5_midmpc_runs():
    """V1: M5 Mid-MPC/BC-MPC runs on main path (not geometric fallback)."""
    import subprocess, json
    r = subprocess.run(
        ["bash", "tools/sil/v1_m5_midmpc_check.sh"],
        capture_output=True, text=True, timeout=420,
    )
    assert r.returncode == 0, f"V1 fail: {r.stderr}\n{r.stdout}"
    with open("/tmp/v1_m5_midmpc_result.json") as f:
        result = json.load(f)
    assert result["verdict"] == "PASS"
    assert result["mpc_count"] >= result["samples"] * 0.7
    assert result["max_waypoints"] >= 5
```

### Step E1.3: Commit V1

- [ ] **Run + commit**

```bash
git add tools/sil/v1_m5_midmpc_check.sh
git commit -m "$(cat <<'EOF'
test(V1): M5 Mid-MPC runs-on-main-path verification

300s sampling of /l3/m5/avoidance_plan.rationale + waypoint count.
PASS criteria: ≥70% samples on MPC path, max waypoints ≥5 (geometric
fallback emits only 2-3). Verifies Plan A merge actually unblocked M5.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task E2: V2 — M7 Doer-Checker 独立性审计

**目标**：M7 不 import M5/M4 算法实现（仅订阅 topic），保证 IEC 61508 SIL 2 Doer-Checker 独立性。

### Step E2.1: 写静态独立性脚本

- [ ] **Create `tools/sil/v2_m7_independence_audit.sh`**

```bash
#!/usr/bin/env bash
# V2 — M7 Safety Supervisor must NOT import M4/M5 internal algorithm files.
# Topic subscriptions (.msg/.proto) are OK; including .hpp from m4/m5/src is FAIL.
set -eu

M7_DIR="src/l3_tdl_kernel/m7_safety_supervisor"
RESULT_FILE="${RESULT_FILE:-/tmp/v2_m7_independence_result.json}"

echo "[V2] Auditing ${M7_DIR} for M4/M5 algorithm coupling..."

# Forbidden: any include of m4_behavior_arbiter or m5_tactical_planner internal headers
violations=$(grep -rnE '#include.*"m4_behavior_arbiter|"m5_tactical_planner|<m4_|<m5_' \
  "$M7_DIR/src" "$M7_DIR/include" 2>/dev/null \
  | grep -v "_msgs\|_pb\|msg/\|/msg" \
  || true)

# Allowed: anything in {std,rclcpp,sil_proto,l3_msgs}
allowed_includes=$(grep -rE '#include' "$M7_DIR/src" "$M7_DIR/include" 2>/dev/null \
  | grep -cE 'rclcpp|<std|<chrono|<memory|_msgs|_pb|sil_proto' || echo 0)

violation_count=$(echo -n "$violations" | grep -c '.' || echo 0)

cat > "$RESULT_FILE" <<EOF
{
  "violations": "${violations}",
  "violation_count": ${violation_count},
  "allowed_include_count": ${allowed_includes},
  "verdict": "$([ "$violation_count" -eq 0 ] && echo PASS || echo FAIL)"
}
EOF
cat "$RESULT_FILE"

if [ "$violation_count" -eq 0 ]; then
  echo "[V2] PASS: M7 has no M4/M5 algorithm coupling (${allowed_includes} allowed includes)"
  exit 0
else
  echo "[V2] FAIL: ${violation_count} violations:"
  echo "$violations"
  exit 1
fi
```

- [ ] **chmod + run baseline**

```bash
chmod +x tools/sil/v2_m7_independence_audit.sh
bash tools/sil/v2_m7_independence_audit.sh
```

Expected: `PASS, violation_count=0`

### Step E2.2: pytest wrapper

- [ ] **Add to e2e test**

```python
def test_V2_m7_doer_checker_independence():
    """V2: M7 has no algorithmic coupling to M4/M5 source headers."""
    import subprocess, json
    r = subprocess.run(
        ["bash", "tools/sil/v2_m7_independence_audit.sh"],
        capture_output=True, text=True, timeout=30,
    )
    assert r.returncode == 0, f"V2 fail: {r.stdout}\n{r.stderr}"
    with open("/tmp/v2_m7_independence_result.json") as f:
        result = json.load(f)
    assert result["verdict"] == "PASS"
    assert result["violation_count"] == 0
```

### Step E2.3: Commit V2

- [ ] **Commit**

```bash
git add tools/sil/v2_m7_independence_audit.sh
git commit -m "$(cat <<'EOF'
test(V2): M7 Doer-Checker independence static audit

Grep M7 source/include for forbidden M4/M5 internal header includes.
Only sil_proto/l3_msgs (DDS contracts) + rclcpp/std are allowed.
Required by IEC 61508 SIL 2 + Architecture §12 ADR-2.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task E3: V3 — M2 World Model 真发布 threats

**目标**：imazu-01-ho 期间 `/l3/m2/threat_state` 或 `/l3/m2/threats` 必须真发布且含 TS1 entry，否则下游 W6 (bridge LATCH release on CPA cleared) 无数据可用。

### Step E3.1: 写 V3 验证脚本

- [ ] **Create `tools/sil/v3_m2_threats_check.sh`**

```bash
#!/usr/bin/env bash
# V3 — M2 World Model must publish /l3/m2/threat_state with TS1 entry during imazu-01-ho.
set -eu

CONTAINER="${CONTAINER:-mass-l3-tacticallayer-sil-nodes-1}"
RESULT_FILE="${RESULT_FILE:-/tmp/v3_m2_threats_result.json}"
TIMEOUT=10

echo "[V3] Checking /l3/m2/threat_state publication & content..."

# Topic existence
topic_exists=$(docker exec "$CONTAINER" bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 topic list" 2>/dev/null \
  | grep -c "^/l3/m2/threat_state$" || echo 0)

# Sample one message
msg=$(docker exec "$CONTAINER" bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout ${TIMEOUT} ros2 topic echo /l3/m2/threat_state --once" 2>/dev/null || echo "")

has_threats=$(echo "$msg" | grep -c "mmsi:\|target_id:\|threat_id:" || echo 0)
has_cpa=$(echo "$msg" | grep -c "cpa\|min_cpa" || echo 0)
has_cpa_status=$(echo "$msg" | grep -c "cpa_status\|status:" || echo 0)

# Rate check (5s window)
rate_output=$(docker exec "$CONTAINER" bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout 7 ros2 topic hz /l3/m2/threat_state" 2>&1 | grep -oE "average rate: [0-9.]+" | head -1 || echo "average rate: 0.0")
rate=$(echo "$rate_output" | grep -oE "[0-9.]+" || echo "0")

cat > "$RESULT_FILE" <<EOF
{
  "topic_exists": ${topic_exists},
  "has_threats": ${has_threats},
  "has_cpa": ${has_cpa},
  "has_cpa_status": ${has_cpa_status},
  "rate_hz": ${rate},
  "verdict": "$([ "$topic_exists" -gt 0 ] && [ "$has_threats" -gt 0 ] && [ "$has_cpa" -gt 0 ] && echo PASS || echo FAIL)"
}
EOF
cat "$RESULT_FILE"

if [ "$topic_exists" -gt 0 ] && [ "$has_threats" -gt 0 ] && [ "$has_cpa" -gt 0 ]; then
  echo "[V3] PASS: M2 publishes threats with CPA at ${rate} Hz"
  exit 0
else
  echo "[V3] FAIL: topic_exists=${topic_exists} has_threats=${has_threats} has_cpa=${has_cpa}"
  exit 1
fi
```

### Step E3.2: pytest wrapper + commit

- [ ] **Add to e2e test**

```python
def test_V3_m2_world_model_threats():
    """V3: M2 publishes /l3/m2/threat_state with TS1 entry + CPA fields."""
    import subprocess, json
    r = subprocess.run(
        ["bash", "tools/sil/v3_m2_threats_check.sh"],
        capture_output=True, text=True, timeout=60,
    )
    assert r.returncode == 0, f"V3 fail: {r.stdout}\n{r.stderr}"
    with open("/tmp/v3_m2_threats_result.json") as f:
        result = json.load(f)
    assert result["verdict"] == "PASS"
    assert result["topic_exists"] >= 1
    assert result["has_threats"] >= 1
    assert result["rate_hz"] >= 0.5  # at least 0.5 Hz
```

- [ ] **Commit**

```bash
git add tools/sil/v3_m2_threats_check.sh
git commit -m "$(cat <<'EOF'
test(V3): M2 World Model threats publication verification

ros2 topic echo + hz check on /l3/m2/threat_state. PASS = topic
exists, contains threats with mmsi/target_id, cpa field present,
rate ≥ 0.5 Hz during imazu-01-ho session.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task E4: 9 断言端到端 pytest

### Step E4.1: 写 helper 模块

- [ ] **Create `tools/sil/_e2e_helpers.py`**

```python
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
        body = r.read()
        return json.loads(body) if body else {}


def _topic_echo_once(topic: str, timeout_s: int = 5) -> dict | None:
    """ros2 topic echo --once via docker exec, return parsed YAML as dict (best effort)."""
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
    """Fallback YAML parse: simple key:value lines."""
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
    """Sample `n` messages spaced across [from_sim_t, to_sim_t] sim-time window.
    Polls sim_time via /api/v1/lifecycle/status. Returns empty list on failure.
    """
    samples: list[dict] = []
    interval = (to_sim_t - from_sim_t) / max(1, n)
    # Wait until sim_time >= from_sim_t
    while True:
        st = _get("/lifecycle/status")
        if st.get("sim_time_s", 0) >= from_sim_t:
            break
        if st.get("current_state") != "active":
            return []
        time.sleep(1)
    for i in range(n):
        target_t = from_sim_t + interval * i
        while _get("/lifecycle/status").get("sim_time_s", 0) < target_t:
            time.sleep(0.5)
            if _get("/lifecycle/status").get("current_state") != "active":
                break
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
```

### Step E4.2: 写 9 断言 + 3 V 测试

- [ ] **Create `tools/sil/test_demo1_imazu01ho_e2e.py`**

```python
"""DEMO-1 imazu-01-ho 端到端物理验收测试。

成功条件：A-1 ~ A-9 + V1-V3 全 PASS。任何一项 FAIL → DEMO-1 未达标。
跑通命令：
  npm run sys:start
  sleep 60
  pytest tools/sil/test_demo1_imazu01ho_e2e.py -v
"""
import math
import pytest
import subprocess
import json
from tools.sil._e2e_helpers import (
    _get, _post, _collect_topic, _topic_echo_once, _wait_until_sim_t,
)

SCENARIO = "imazu-01-ho"


@pytest.fixture(scope="module", autouse=True)
def lifecycle_session():
    """Reset + configure + activate scenario; tear down after all tests."""
    _post("/lifecycle/cleanup")
    cfg = _post("/lifecycle/configure", {"scenario_id": SCENARIO})
    assert cfg.get("success"), f"configure failed: {cfg}"
    act = _post("/lifecycle/activate")
    assert act.get("success"), f"activate failed: {act}"
    yield
    _post("/lifecycle/cleanup")


# ============================================================
# A-1 ~ A-9 — Demo-1场景.md 5 阶段物理验收
# ============================================================

def test_A1_transit_straight():
    """§1 (T+0~200s): own_heading 不偏离北向超 5°"""
    samples = _collect_topic("/sil/own_ship_state", from_sim_t=10, to_sim_t=190, n=10)
    assert len(samples) >= 5, f"only {len(samples)} samples collected"
    max_dev = max(abs(float(s.get("heading", 0))) for s in samples)
    assert max_dev <= math.radians(5.0), \
        f"§1 transit heading drift {math.degrees(max_dev):.2f}° > 5°"


def test_A2_rule14_triggered():
    """§2 (T+180~320s): applicable_rule 切到 Rule 14"""
    samples = _collect_topic("/l3/m6/rule_assessment", from_sim_t=180, to_sim_t=320, n=20)
    rules = [s.get("applicable_rule", "") for s in samples]
    assert any("Rule 14" in str(r) for r in rules), \
        f"§2 Rule 14 never triggered, observed rules: {set(rules)}"


def test_A3_starboard_turn():
    """§3 (T+200~500s): own_heading 右偏到 25-45° 之间（含上限防过避）"""
    samples = _collect_topic("/sil/own_ship_state", from_sim_t=200, to_sim_t=500, n=30)
    headings_rad = [float(s.get("heading", 0)) for s in samples]
    max_hdg = max(headings_rad)
    assert math.radians(25) <= max_hdg <= math.radians(45), \
        f"§3 turn magnitude {math.degrees(max_hdg):.1f}° outside [25, 45]"


def test_A4_safe_cpa():
    """§4 min CPA ≥ 500m"""
    # Wait sim past 600s to ensure CPA closure observed
    _wait_until_sim_t(620)
    s = _get("/scoring/last_run")
    assert s.get("kpis") is not None, f"scoring kpis null: {s}"
    min_cpa_nm = s["kpis"]["min_cpa_nm"]
    min_cpa_m = min_cpa_nm * 1852.0
    assert min_cpa_m >= 500.0, f"min_cpa={min_cpa_m:.0f}m < 500m (= {min_cpa_nm:.4f}nm)"


def test_A5_return_to_nominal():
    """§4 末 (t=650s): heading 回归 0° ±5°"""
    _wait_until_sim_t(650)
    msg = _topic_echo_once("/sil/own_ship_state")
    assert msg is not None, "own_ship_state topic silent at t=650s"
    hdg = float(msg.get("heading", 999))
    assert abs(hdg) <= math.radians(5.0), \
        f"§4 return heading {math.degrees(hdg):.1f}° not within ±5°"


def test_A6_auto_stop():
    """§5: 到 sim_time=710s 时 lifecycle 已 inactive"""
    _wait_until_sim_t(710)
    status = _get("/lifecycle/status")
    assert status["current_state"] == "inactive", \
        f"lifecycle still '{status['current_state']}' at sim_t=710s"


def test_A7_scoring_complete():
    """§5: scoring 返回 verdict + dimensions"""
    s = _get("/scoring/last_run")
    assert s.get("kpis") is not None, f"kpis null: {s}"
    assert s.get("scoring_dimensions") is not None, f"scoring_dimensions null: {s}"
    assert s.get("verdict") in {"pass", "fail"}, f"unexpected verdict: {s.get('verdict')}"


def test_A8_decision_chain_real():
    """§3 期间 M4 不在 IvP infeasible fallback"""
    samples = _collect_topic("/l3/m4/behavior_plan", from_sim_t=300, to_sim_t=400, n=10)
    assert len(samples) >= 3
    for s in samples:
        rat = str(s.get("rationale", ""))
        assert "IvP infeasible" not in rat, \
            f"M4 still in fallback: rationale={rat!r}"


def test_A9_no_demo_path():
    """demo dead-reckoning 已下线"""
    r = subprocess.run(
        ["grep", "-c", "demo", "src/sil_orchestrator/main.py"],
        capture_output=True, text=True,
    )
    n = int(r.stdout.strip() or "0")
    assert n == 0, f"demo references remain in main.py: {n}"
    # demo files deleted
    import os
    assert not os.path.exists("src/sil_orchestrator/demo_avoidance.py")
    assert not os.path.exists("src/sil_orchestrator/demo_scorer.py")


# ============================================================
# V1 ~ V3 — Validation suite (rerun the bash scripts)
# ============================================================

def test_V1_m5_midmpc_runs():
    """V1: M5 Mid-MPC runs on main path."""
    r = subprocess.run(
        ["bash", "tools/sil/v1_m5_midmpc_check.sh"],
        capture_output=True, text=True, timeout=420,
    )
    assert r.returncode == 0, f"V1 fail:\n{r.stdout}\n{r.stderr}"


def test_V2_m7_independence():
    """V2: M7 has no algorithmic coupling to M4/M5."""
    r = subprocess.run(
        ["bash", "tools/sil/v2_m7_independence_audit.sh"],
        capture_output=True, text=True, timeout=30,
    )
    assert r.returncode == 0, f"V2 fail:\n{r.stdout}\n{r.stderr}"


def test_V3_m2_threats():
    """V3: M2 World Model publishes threats."""
    r = subprocess.run(
        ["bash", "tools/sil/v3_m2_threats_check.sh"],
        capture_output=True, text=True, timeout=60,
    )
    assert r.returncode == 0, f"V3 fail:\n{r.stdout}\n{r.stderr}"
```

### Step E4.3: 跑 baseline（应全 FAIL，因为 A/B/C/D 未 merge）

- [ ] **Run + 记录红 baseline**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
npm run sys:start
sleep 60
pytest tools/sil/test_demo1_imazu01ho_e2e.py -v 2>&1 | tee /tmp/e2e_baseline_red.log
```

Expected: 多数 FAIL（A-2/A-3/A-5/A-6/A-8/V1 等，因主线未修）

### Step E4.4: Commit e2e test

- [ ] **Commit**

```bash
git add tools/sil/_e2e_helpers.py tools/sil/test_demo1_imazu01ho_e2e.py
git commit -m "$(cat <<'EOF'
test(e2e): DEMO-1 imazu-01-ho 9 断言端到端 + V1-V3 验证

A-1~A-9 覆盖 Demo-1场景.md 五阶段物理验收：transit / Rule 14 /
starboard / CPA≥500m / return-to-nominal / auto-stop / scoring /
no-fallback / demo-cleanup。V1-V3 验证模块真跑（非 stub）。

跑通命令：pytest tools/sil/test_demo1_imazu01ho_e2e.py -v
TDD red baseline：A/B/C/D 未 merge 前应多数 FAIL（已记录在
/tmp/e2e_baseline_red.log）。merge 后应全绿。

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task E5: 跑端到端 + 写 verdict 报告

### Step E5.1: 确保 A/B/C/D 全 merge 到 main

- [ ] **检查 main 含 4 个 plan 的所有 W commits**

```bash
git log --oneline main | grep -E "feat\(W[0-9]+\)|refactor\(W10\)" | wc -l
# Expected: ≥10 commits (W1..W10)
git log --oneline main | head -20
```

### Step E5.2: 跑完整 e2e（≥ 12 分钟实时）

- [ ] **重启容器 + activate + pytest 全跑**

```bash
npm run sys:stop || true
sleep 5
npm run sys:start
sleep 60
pytest tools/sil/test_demo1_imazu01ho_e2e.py -v 2>&1 | tee /tmp/e2e_final.log
```

Expected duration: ~12-15 分钟（含 700s sim run）

### Step E5.3: 写 verdict 报告

- [ ] **Create `docs/Design/Review/2026-05-27/R6-e2e-results.md`**

```markdown
# R6 DEMO-1 端到端验收结果

| 字段 | 值 |
|---|---|
| 运行日期 | <YYYY-MM-DD> |
| Git HEAD | <commit hash> |
| 运行命令 | `pytest tools/sil/test_demo1_imazu01ho_e2e.py -v` |
| 总运行时长 | <X> min <Y>s |
| 结果 | <PASS / FAIL> |

## 12 断言矩阵（A-1~A-9 + V1-V3）

| ID | 描述 | 结果 | 实测值 | 阈值 |
|---|---|---|---|---|
| A-1 | §1 直航 | <PASS/FAIL> | max_dev=<X>° | ≤5° |
| A-2 | §2 Rule 14 触发 | <PASS/FAIL> | 触发时刻 t=<X>s | t∈[180,320] |
| A-3 | §3 右舷转向 | <PASS/FAIL> | max_hdg=<X>° | 25-45° |
| A-4 | §4 安全 CPA | <PASS/FAIL> | min_cpa=<X>m | ≥500m |
| A-5 | §4 归航 | <PASS/FAIL> | t=650s heading=<X>° | ≤5° |
| A-6 | §5 auto-stop | <PASS/FAIL> | t=710s state=<X> | inactive |
| A-7 | §5 scoring | <PASS/FAIL> | verdict=<X> | pass/fail |
| A-8 | M4 决策链真打通 | <PASS/FAIL> | 0 IvP infeasible 样本 | 0 |
| A-9 | demo 已下线 | <PASS/FAIL> | grep count=<X> | 0 |
| V1 | M5 Mid-MPC 真跑 | <PASS/FAIL> | mpc_count=<X>/<N> | ≥70% |
| V2 | M7 独立性 | <PASS/FAIL> | violations=<X> | 0 |
| V3 | M2 threats | <PASS/FAIL> | rate=<X>Hz | ≥0.5Hz |

## 失败处置

任何一条 FAIL：
1. 收集失败 test 的 stdout/stderr 到本报告附录
2. 回 Phase 1 systematic-debugging 重新分析
3. 必要时开新 D-task（如 V1 fail → D-DEMO1-R7 M5 Mid-MPC 集成）
```

- [ ] **Commit results**

```bash
git add docs/Design/Review/2026-05-27/R6-e2e-results.md
git commit -m "$(cat <<'EOF'
docs(demo1): R6 端到端验收结果（<PASS/FAIL>）

12 断言矩阵 + 实测值。跑通时长 <X>min。
$([ "<PASS/FAIL>" = "PASS" ] && echo "DEMO-1 验收通过，可演示。" || echo "失败处置见报告。")

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task E6: 后置 — Phase 1 progress 文档更新

### Step E6.1: 更新 Phase 1 overview

- [ ] **Modify `docs/Design/Phase 1/00-overview.md`**

把 D-DEMO1-R6 状态从 pending → in_progress → completed（按实际进度更新），并在 D 任务索引加 R6 链接。具体行号 + 内容由执行 agent 在本步 Read 后填写。

### Step E6.2: 更新各 M{n}-progress.md

- [ ] **更新触及的 M 模块 progress**

| 文件 | 加入行 |
|---|---|
| `docs/Design/Phase 1/TDL-Kernel/M3-mission-manager/M3-progress.md` | D-DEMO1-R6 W2/W3 — TaskValidity 子状态实现 |
| `docs/Design/Phase 1/TDL-Kernel/M4-behavior-arbiter/M4-progress.md` | D-DEMO1-R6 W4 — fallback snapshot 绝对化 + SafetyConcernEvent |
| `docs/Design/Phase 1/TDL-Kernel/M6-colregs-reasoner/M6-progress.md` | D-DEMO1-R6 W5 — Rule 14 head-on 三条件分类器 |
| `docs/Design/Phase 1/TDL-Kernel/M1-odd-envelope-manager/M1-progress.md` | D-DEMO1-R6 W9 — 15s 看门狗监 M3 ACTIVE |

每文件 ≤10 行追加，含 commit hash 引用。

### Step E6.3: Commit progress 更新

- [ ] **Commit**

```bash
git add "docs/Design/Phase 1/00-overview.md" "docs/Design/Phase 1/TDL-Kernel/"
git commit -m "$(cat <<'EOF'
docs(phase1): R6 完成后 progress 同步

00-overview.md + M1/M3/M4/M6 各 progress.md 标注 D-DEMO1-R6 完成。

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## 验收 Definition of Done

- [ ] V1-V3 三脚本可独立 bash 跑出 verdict
- [ ] 9 + 3 = 12 断言 pytest 全绿
- [ ] R6-e2e-results.md 已填写完整矩阵
- [ ] Phase 1 progress + 各 M progress 文档已同步
- [ ] DEMO-1 演示视频录制（≥ 5min，覆盖五阶段）— **可选独立 task**

## 失败回退路径

| 失败 | 处置 |
|---|---|
| V1 FAIL（M5 仍 fallback） | 开 D-DEMO1-R7 专修 M5 Mid-MPC 集成（spec §8 已预算） |
| V2 FAIL（M7 含 M4/M5 头文件） | 紧急 hotfix 拆耦合 + 重审 ADR-2 |
| V3 FAIL（M2 不发 threats） | 回 Phase 1 调查 M2 lifecycle / wiring |
| A-2 FAIL（Rule 14 永不触发） | 回 Plan A W5 调 bearing rate / heading 阈值 |
| A-4 FAIL（CPA < 500m） | 严重——调 Plan B W6 + Plan A W4 fallback 触发更早 |
| A-5 FAIL（不归航） | 调 Plan B W6 LATCH 释放条件或下降速率 |
| A-6 FAIL（不 auto-stop） | 调 Plan B W7 duration timer |
| 多项 FAIL（≥3） | 回 Phase 1 systematic-debugging 全面分析 |
