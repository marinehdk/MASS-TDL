# Fail-Loud SIL Node Parameter Defaults — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除 ship_dynamics_node 和 target_vessel_node 的 YAML/参数加载 silent fallback 风险，将失败静默行为改为 fail-loud（fatal 日志 + 容器 exit 1）。

**Architecture:** 在 `_load_coefficients()` / `on_configure()` 中将关键参数默认值改为非法 sentinel（lat=-999.0, u0=-1.0 等），加载后强制校验必填字段非 sentinel；`on_configure()` 失败时返回 `TransitionCallbackReturn.FAILURE`；入口脚本检测到 FAILURE 后容器 `exit 1`。

**Tech Stack:** Python 3.10+, ROS2 Humble (rclpy LifecycleNode), pytest, bash

**涉及模块:** ship_dynamics (M2 消费方), target_vessel (M2 消费方), sil_entrypoint (基础设施)

**依赖:** B2 (ScenarioAuthoringNode 显式 param 注入) 关闭后执行 — 否则 sentinel 校验会始终触发 FAILURE。

---

## 文件结构总览

| 文件 | 动作 | 职责 |
|------|------|------|
| `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py` | **修改** | 添加 sentinel 默认值 + 校验逻辑 + 加载值 echo |
| `src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py` | **修改** | 添加目标字段校验 + 加载值 echo |
| `tests/sim_workbench/sil_nodes/test_fail_loud_defaults.py` | **新建** | 3 case 集成测试（无参 / 部分参 / 完整参）|
| `tests/sim_workbench/sil_nodes/ship_dynamics/test_lifecycle.py` | **修改** | 注入完整参数使现有测试继续通过 |
| `tests/sim_workbench/sil_nodes/target_vessel/test_lifecycle.py` | **修改** | 同上（如需要）|
| `docker/sil_entrypoint.sh` | **修改** | Stage 1 后显式调用 on_configure 并检测 FAILURE → exit 1 |

---

### Task 1: ship_dynamics_node fail-loud 改造

**Files:**
- Modify: `src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py` (lines 82-264)

- [ ] **Step 1: 添加 sentinel 常量**

在文件顶部（`import` 之后）添加 sentinel 常量：

```python
# ── Sentinel values for fail-loud parameter validation ─────────
_SENTINEL_LAT = -999.0       # 纬度非法 sentinel
_SENTINEL_LON = -999.0       # 经度非法 sentinel
_SENTINEL_U0  = -1.0         # 初速非法 sentinel (m/s)
_SENTINEL_DT  = -1.0         # 步长非法 sentinel (s)
```

- [ ] **Step 2: 修改 `_load_coefficients()` — sentinel 默认值 + 精确异常处理**

将 `_load_coefficients()` (line 230-264) 改为：

```python
def _load_coefficients(self) -> MMGCoefficients:
    """从 ROS 参数服务器加载 MMG 系数。

    关键初始参数使用非法 sentinel 默认值；场景注入（ScenarioAuthoringNode）
    必须在 declare_parameter 前设置参数，否则 sentinel 会触发后续校验失败。

    Raises:
        rclpy.exceptions.ParameterNotDeclaredException: 参数未声明时调用 get_parameter
    """
    from rclpy.exceptions import ParameterAlreadyDeclaredException

    coeffs = MMGCoefficients()

    # 关键初始状态参数使用 sentinel 默认值
    # 物理模型参数保留 dataclass 硬编码默认值
    sentinel_overrides = {
        "origin_lat": _SENTINEL_LAT,
        "origin_lon": _SENTINEL_LON,
        "u0": _SENTINEL_U0,
        "x0": 0.0,       # x0/y0 可为 0（以原点为参考）
        "y0": 0.0,
        "psi0": 0.0,      # heading sentinel（arcsin 超出 [-2π, 2π]）
        "dt": _SENTINEL_DT,
    }

    param_map = {
        "L": "L", "d": "d", "B": "B",
        # ... (same as before)
        "x0": "x0", "y0": "y0", "psi0": "psi0", "u0": "u0",
        "origin_lat": "origin_lat", "origin_lon": "origin_lon",
    }
    for py_attr, ros_param in param_map.items():
        try:
            default_val = sentinel_overrides.get(py_attr, getattr(coeffs, py_attr))
            declared = self.declare_parameter(ros_param, default_val)
        except ParameterAlreadyDeclaredException:
            # 场景已在外部声明此参数（如 ScenarioAuthoringNode / YAML）
            declared = self.get_parameter(ros_param)
        setattr(coeffs, py_attr, declared.value)
    return coeffs
```

**关键变更:**
- `except Exception: pass` → `except ParameterAlreadyDeclaredException:` 精确捕获
- 关键初始参数 (`origin_lat`, `origin_lon`, `u0`, `psi0`, `dt`) 默认值改为 sentinel
- 物理模型参数 (L, d, B, X_vv...) 保留 dataclass 默认值

- [ ] **Step 3: 修改 `on_configure()` — 添加 sentinel 校验 + 加载值 echo**

在 `on_configure()` (line 82-101) 中，`_load_coefficients()` 成功后添加校验和 echo：

```python
def on_configure(self, state: State) -> TransitionCallbackReturn:
    """加载 MMG 参数，创建物理模型和初始状态。

    校验关键初始参数非 sentinel 值（sentinel 表示 ScenarioAuthoringNode
    未正确注入场景参数）。
    """
    try:
        coeffs = self._load_coefficients()
        # ── sentinel 校验 ──
        errors = []
        if coeffs.origin_lat == _SENTINEL_LAT or not (-90.0 <= coeffs.origin_lat <= 90.0):
            errors.append(f"origin_lat={coeffs.origin_lat} (sentinel or out of range [-90,90])")
        if coeffs.origin_lon == _SENTINEL_LON or not (-180.0 <= coeffs.origin_lon <= 180.0):
            errors.append(f"origin_lon={coeffs.origin_lon} (sentinel or out of range [-180,180])")
        if coeffs.u0 <= 0.0:
            errors.append(f"u0={coeffs.u0} (must be > 0, sentinel or not set)")
        if coeffs.dt <= 0.0:
            errors.append(f"dt={coeffs.dt} (must be > 0, sentinel or not set)")
        if coeffs.psi0 < -6.28319 or coeffs.psi0 > 6.28319:
            errors.append(f"psi0={coeffs.psi0} (out of range [-2π, 2π], possible sentinel)")

        if errors:
            for err in errors:
                self.get_logger().fatal(f"Required parameter missing/invalid: {err}")
            return TransitionCallbackReturn.FAILURE

        self._model = MMGModel(coeffs)
        self._state = ShipState(
            x=coeffs.x0, y=coeffs.y0, psi=coeffs.psi0, phi=0.0,
            u=coeffs.u0, v=0.0, r=0.0, p=0.0,
        )
        self._origin_lat_rad = math.radians(coeffs.origin_lat)
        self._origin_lon_rad = math.radians(coeffs.origin_lon)
        self._n_rps_cmd = coeffs.u0 / (coeffs.D_P * 3.0)
    except Exception as exc:
        if hasattr(self, "get_logger"):
            self.get_logger().fatal(f"on_configure 失败: {exc}")
        return TransitionCallbackReturn.FAILURE

    # ── 加载值 echo (debug-friendly) ──
    if hasattr(self, "get_logger"):
        self.get_logger().info(
            f"ship_dynamics initial: "
            f"origin=({coeffs.origin_lat:.4f},{coeffs.origin_lon:.4f}) "
            f"u0={coeffs.u0:.2f}m/s psi0={coeffs.psi0:.4f}rad "
            f"x0={coeffs.x0:.1f}m y0={coeffs.y0:.1f}m "
            f"dt={coeffs.dt:.3f}s"
        )
    return TransitionCallbackReturn.SUCCESS
```

- [ ] **Step 4: 更新现有测试 `test_lifecycle.py` — 注入完整参数**

现有 `test_on_configure_returns_success` 会在 sentinel 默认值下失败。修改 fixture 预注入完整参数：

```python
@pytest.fixture
def node():
    if not _has_ros2():
        pytest.skip("rclpy 不可用 (非 ROS2 环境)")
    rclpy.init(args=None)
    from ship_dynamics.node import ShipDynamicsNode
    from rclpy.parameter import Parameter
    n = ShipDynamicsNode(node_name="test_ship_dynamics")
    # 预注入完整参数以满足 sentinel 校验
    n.declare_parameter("origin_lat", 63.44)
    n.declare_parameter("origin_lon", 10.38)
    n.declare_parameter("u0", 9.26)
    n.declare_parameter("dt", 0.02)
    yield n
    try:
        n.destroy_node()
    except Exception:
        pass
    rclpy.shutdown()
```

**注意:** 由于 `declare_parameter` 在 ROS2 中不允许重复声明，`_load_coefficients()` 中已改为捕获 `ParameterAlreadyDeclaredException` 并使用 `get_parameter()` 读取已声明值，因此此 fixture 可正常工作。

- [ ] **Step 5: 提交 Task 1**

```bash
git add src/sim_workbench/sil_nodes/ship_dynamics/ship_dynamics/node.py \
        tests/sim_workbench/sil_nodes/ship_dynamics/test_lifecycle.py
git commit -m "feat(ship_dynamics): fail-loud parameter validation with sentinel defaults

- Add sentinel constants (_SENTINEL_LAT=-999.0 etc) for critical initial params
- Replace bare except:pass in _load_coefficients() with precise ParameterAlreadyDeclaredException handling
- Key params (origin_lat, origin_lon, u0, dt) default to sentinels instead of valid values
- Add sentinel validation in on_configure() — FAILURE if any sentinel detected
- Add info-level echo of all loaded initial values for debug traceability
- Update existing test fixture to pre-declare valid params (sentinel-safe)
- Fixes: silent fallback to hardcoded defaults (e.g., origin_lat=30.5 instead of imazu's 63.44)"
```

---

### Task 2: target_vessel_node fail-loud 改造

**Files:**
- Modify: `src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py` (lines 127-138)

- [ ] **Step 1: 修改 `on_configure()` — 添加目标字段校验 + 加载值 echo**

```python
def on_configure(self, state: LifecycleState) -> TransitionCallbackReturn:
    self.declare_parameter("default_targets_json", "[]")
    raw = self.get_parameter("default_targets_json").value
    if raw and raw != "[]":
        try:
            entries = json.loads(raw)
            if not isinstance(entries, list):
                self.get_logger().fatal(
                    f"default_targets_json must be a JSON array, got {type(entries).__name__}"
                )
                return TransitionCallbackReturn.FAILURE
            for i, entry in enumerate(entries):
                # ── 必填字段校验 ──
                mmsi = entry.get("mmsi", 0)
                lat = entry.get("lat", 0.0)
                lon = entry.get("lon", 0.0)
                heading_deg = entry.get("heading_deg", -1.0)
                sog_kn = entry.get("sog_kn", -1.0)

                errors = []
                if not isinstance(mmsi, int) or mmsi <= 0:
                    errors.append(f"mmsi={mmsi} (must be > 0)")
                if not isinstance(lat, (int, float)) or math.isnan(float(lat)) or not (-90.0 <= lat <= 90.0) or lat == 0.0:
                    errors.append(f"lat={lat} (must be in [-90,90] and != 0 sentinel)")
                if not isinstance(lon, (int, float)) or math.isnan(float(lon)) or not (-180.0 <= lon <= 180.0):
                    errors.append(f"lon={lon} (must be in [-180,180])")
                if not isinstance(heading_deg, (int, float)) or not (0.0 <= heading_deg < 360.0):
                    errors.append(f"heading_deg={heading_deg} (must be in [0,360))")
                if not isinstance(sog_kn, (int, float)) or sog_kn < 0.0:
                    errors.append(f"sog_kn={sog_kn} (must be >= 0)")

                if errors:
                    for err in errors:
                        self.get_logger().fatal(
                            f"Target #{i} (mmsi={entry.get('mmsi', 'N/A')}) invalid field: {err}"
                        )
                    return TransitionCallbackReturn.FAILURE

                self.add_target(**entry)
        except (json.JSONDecodeError, TypeError, KeyError) as exc:
            self.get_logger().fatal(f"Failed to parse default_targets_json: {exc}")
            return TransitionCallbackReturn.FAILURE

    # ── 加载值 echo (debug-friendly) ──
    target_summary = ", ".join(
        f"#{t.mmsi}@({t.lat:.4f},{t.lon:.4f}) h={math.degrees(t.heading):.1f}° sog={t.sog/0.514444:.1f}kn"
        for t in self._targets
    ) if self._targets else "(none)"
    self.get_logger().info(
        f"target_vessel initial: {len(self._targets)} target(s) — {target_summary}"
    )
    return TransitionCallbackReturn.SUCCESS
```

- [ ] **Step 2: 更新现有测试 `test_lifecycle.py` — 确保空 targets 场景仍 SUCCESS**

`default_targets_json="[]"` 是合法状态（无目标船），现有测试应该继续通过。但需验证空 JSON 数组不会被误判为 FAILURE。确认逻辑正确后无需修改测试。

- [ ] **Step 3: 提交 Task 2**

```bash
git add src/sim_workbench/sil_nodes/target_vessel/target_vessel/node.py
git commit -m "feat(target_vessel): fail-loud target field validation + loading echo

- Add per-target field validation in on_configure() (mmsi>0, lat in [-90,90] !=0, lon, heading, sog)
- Log fatal and return FAILURE on any invalid target field
- Add info-level echo of all loaded target vessel initial states
- Empty targets (JSON '[]') remains valid SUCCESS case
- Fixes: silent acceptance of targets with zero/NaN/sentinel fields"
```

---

### Task 3: 集成测试 `test_fail_loud_defaults.py`

**Files:**
- Create: `tests/sim_workbench/sil_nodes/test_fail_loud_defaults.py`

- [ ] **Step 1: 创建测试文件**

```python
"""Fail-loud 参数默认值测试 — 验证 sentinel 校验逻辑在无参/部分参/完整参场景下行为正确。

所有测试需要 ROS2 环境（rclpy）。"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

# Ensure packages are importable
_sil_nodes = Path(__file__).resolve().parents[3] / "src" / "sim_workbench" / "sil_nodes"
sys.path.insert(0, str(_sil_nodes / "ship_dynamics"))
sys.path.insert(0, str(_sil_nodes / "target_vessel"))

try:
    import rclpy
    from rclpy.lifecycle import State
    from rclpy.parameter import Parameter

    HAS_ROS2 = True
except ImportError:
    HAS_ROS2 = False

_SKIP_REASON = "rclpy not available (non-ROS2 environment)"


class TestShipDynamicsFailLoud:
    """ship_dynamics_node on_configure 行为：无参→FAILURE, 部分参→FAILURE, 完整参→SUCCESS."""

    @pytest.fixture(autouse=True)
    def _ros(self):
        if not HAS_ROS2:
            pytest.skip(_SKIP_REASON)
        rclpy.init()
        yield
        rclpy.shutdown()

    @pytest.fixture
    def make_node(self):
        """每次测试创建独立节点。"""
        from ship_dynamics.node import ShipDynamicsNode

        nodes = []

        def _make():
            n = ShipDynamicsNode(node_name="test_fail_loud_sd")
            nodes.append(n)
            return n

        yield _make
        for n in nodes:
            try:
                n.destroy_node()
            except Exception:
                pass

    def test_no_params_returns_failure(self, make_node):
        """case 1: 不注入任何参数 → on_configure 必须 FAILURE."""
        node = make_node()
        result = node.on_configure(State.PRIMARY_STATE_UNCONFIGURED)
        # sentinel 默认值应触发校验失败
        assert result != rclpy.lifecycle.TransitionCallbackReturn.SUCCESS, (
            "Expected FAILURE with no params injected, got SUCCESS"
        )

    def test_partial_params_returns_failure(self, make_node):
        """case 2: 只注入 origin_lat 不注入 origin_lon → 必须 FAILURE."""
        node = make_node()
        # 预声明部分参数
        node.declare_parameter("origin_lat", 63.44)
        # origin_lon 保持 sentinel (-999.0) → 应触发 FAILURE
        result = node.on_configure(State.PRIMARY_STATE_UNCONFIGURED)
        assert result != rclpy.lifecycle.TransitionCallbackReturn.SUCCESS, (
            "Expected FAILURE with partial params, got SUCCESS"
        )

    def test_full_params_returns_success(self, make_node):
        """case 3: 完整注入 imazu-08 参数 → SUCCESS."""
        node = make_node()
        # 预声明所有关键初始参数 (imazu-08-ms values)
        node.declare_parameter("origin_lat", 63.44)
        node.declare_parameter("origin_lon", 10.38)
        node.declare_parameter("u0", 5.14444)   # 10 kn → m/s
        node.declare_parameter("psi0", 0.0)      # heading 0°
        node.declare_parameter("dt", 0.02)
        result = node.on_configure(State.PRIMARY_STATE_UNCONFIGURED)
        assert result == rclpy.lifecycle.TransitionCallbackReturn.SUCCESS, (
            f"Expected SUCCESS with full imazu-08 params, got {result}"
        )


class TestTargetVesselFailLoud:
    """target_vessel_node on_configure 行为：非法目标字段 → FAILURE, 有效/空 → SUCCESS."""

    @pytest.fixture(autouse=True)
    def _ros(self):
        if not HAS_ROS2:
            pytest.skip(_SKIP_REASON)
        rclpy.init()
        yield
        rclpy.shutdown()

    @pytest.fixture
    def make_node(self):
        from target_vessel.node import TargetVesselNode

        nodes = []

        def _make():
            n = TargetVesselNode()
            nodes.append(n)
            return n

        yield _make
        for n in nodes:
            try:
                n.destroy_node()
            except Exception:
                pass

    def test_empty_targets_succeeds(self, make_node):
        """空 targets 数组是合法状态 → SUCCESS."""
        node = make_node()
        result = node.on_configure(None)
        assert result == rclpy.lifecycle.TransitionCallbackReturn.SUCCESS

    def test_invalid_mmsi_fails(self, make_node):
        """mmsi=0 的目标触发 FAILURE."""
        node = make_node()
        import json
        node.declare_parameter(
            "default_targets_json",
            json.dumps([{"mmsi": 0, "lat": 63.5, "lon": 10.4, "heading_deg": 90, "sog_kn": 10}])
        )
        result = node.on_configure(None)
        assert result != rclpy.lifecycle.TransitionCallbackReturn.SUCCESS

    def test_valid_target_succeeds(self, make_node):
        """合法目标字段 → SUCCESS."""
        node = make_node()
        import json
        node.declare_parameter(
            "default_targets_json",
            json.dumps([{"mmsi": 100000001, "lat": 63.503492, "lon": 10.241335,
                         "heading_deg": 90, "sog_kn": 10}])
        )
        result = node.on_configure(None)
        assert result == rclpy.lifecycle.TransitionCallbackReturn.SUCCESS
```

- [ ] **Step 2: 运行测试验证**

```bash
pytest tests/sim_workbench/sil_nodes/test_fail_loud_defaults.py -v
```
预期: 所有 6 个测试 PASS（需要 ROS2 环境），或在 macOS 上 SKIP。

- [ ] **Step 3: 提交 Task 3**

```bash
git add tests/sim_workbench/sil_nodes/test_fail_loud_defaults.py
git commit -m "test: add fail-loud defaults integration tests (3 cases each for SD/TV)

- ShipDynamics: no params→FAILURE, partial params→FAILURE, full imazu-08→SUCCESS
- TargetVessel: empty targets→SUCCESS, invalid mmsi→FAILURE, valid target→SUCCESS"
```

---

### Task 4: sil_entrypoint.sh — on_configure FAILURE → container exit 1

**Files:**
- Modify: `docker/sil_entrypoint.sh` (Stage 1 后, line ~96)

- [ ] **Step 1: 在 Stage 1 后添加工况校验**

在 `sil_entrypoint.sh` 的 Stage 1 完成行（`print(f'[{ts()}] Stage 1 complete...')`）之后，添加显式 `on_configure` 调用和结果检测：

```python
# ── 插入位置: line 97 之后 (Stage 1 complete 打印后) ──
# 显式触发关键节点的 on_configure 并检测 FAILURE
print(f'[{ts()}] Stage 1.5: Validating node configuration...')
cfg_failures = []
for node in nodes:
    name = node.get_name()
    if name in ('ship_dynamics_node', 'target_vessel_node'):
        try:
            cfg_result = node.on_configure(None)
            if cfg_result is not None and cfg_result != TransitionCallbackReturn.SUCCESS:
                cfg_failures.append(f'{name}: on_configure returned {cfg_result}')
                print(f'  [{ts()}] FATAL: {name} on_configure FAILED → {cfg_result}', file=sys.stderr)
            else:
                print(f'  [{ts()}] {name} on_configure OK')
        except Exception as exc:
            cfg_failures.append(f'{name}: on_configure raised {type(exc).__name__}: {exc}')
            print(f'  [{ts()}] FATAL: {name} on_configure crashed: {exc}', file=sys.stderr)

if cfg_failures:
    print(f'[{ts()}] FATAL: {len(cfg_failures)} node(s) failed on_configure:', file=sys.stderr)
    for f in cfg_failures:
        print(f'  - {f}', file=sys.stderr)
    sys.exit(1)
```

**插入精确位置**：在 `sil_count = len(nodes) - 1` 之前（即 LifecycleManagerNode 和其他节点创建完成后，liveness probe 创建前）。

- [ ] **Step 2: 验证 — 手动删除 scenario YAML 后重启容器**

```bash
# 临时重命名 scenario 文件模拟缺失
mv scenarios/IMAZU标准测试/imazu-08-ms.yaml scenarios/IMAZU标准测试/imazu-08-ms.yaml.bak
# 启动容器，预期 1 min 内 exit 1
# 恢复
mv scenarios/IMAZU标准测试/imazu-08-ms.yaml.bak scenarios/IMAZU标准测试/imazu-08-ms.yaml
```

- [ ] **Step 3: 验证 — 正常路径检查日志**

```bash
docker compose up  # 使用 imazu-08 场景
docker logs <sil-container> 2>&1 | grep "ship_dynamics initial"
```
预期输出包含 `origin=(63.4400,10.3800) u0=5.14m/s psi0=0.0000rad`。

- [ ] **Step 4: 提交 Task 4**

```bash
git add docker/sil_entrypoint.sh
git commit -m "feat(entrypoint): explicit on_configure validation → exit 1 on FAILURE

- After Stage 1 node creation, explicitly call on_configure for ship_dynamics and target_vessel
- If any returns FAILURE, print diagnostic and sys.exit(1)
- Prevents 'zombie simulation' where nodes silently use sentinel defaults
- Fixes: DEMO-1 risk where wrong coordinates appear to work (R3, 50% probability)"
```

---

## 验证检查清单

- [ ] `pytest tests/sim_workbench/sil_nodes/test_fail_loud_defaults.py -v` — 6 tests green
- [ ] `pytest tests/sim_workbench/sil_nodes/ship_dynamics/test_lifecycle.py -v` — 不回归
- [ ] `pytest tests/sim_workbench/sil_nodes/target_vessel/test_lifecycle.py -v` — 不回归
- [ ] 删除 `scenarios/IMAZU标准测试/imazu-08-ms.yaml` → 容器 60s 内 exit 1
- [ ] 正常 `docker compose up` (imazu-08) → `docker logs | grep "ship_dynamics initial"` 显示正确值
- [ ] 正常 `docker compose up` → `docker logs | grep "target_vessel initial"` 显示正确 targets

## 不在范围

- sensor_mock / tracker_mock 节点的 silent default（DEMO-1 不强依赖）
- env_disturbance（DEMO-1 默认值可接受）
- ScenarioAuthoringNode 注入逻辑修复（B2 任务）

## 回滚计划

若 B2 未完成时此变更被误合并：
1. Revert Task 1 commit (sentinel 默认值)
2. 节点会因 sentinel 校验始终 FAILURE
3. 回滚后恢复原有 `except: pass` 行为
