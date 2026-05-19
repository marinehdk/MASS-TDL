# D1.3b.1 · YAML 场景管理基础 + Imazu-22 + Cerberus 双语言 · 执行 Spec

**版本**：v3.1  
**日期**：2026-05-11  
**前版本**：v1.0（2026-05-09，已废弃，保留于 `archive/D1.3b-v1.0-spec-archived.md`）  
**v3.0 D 编号**：D1.3b.1（D1.3b 第一工作单元）  
**Owner**：技术负责人  
**完成日期**：2026-06-15 EOD（DEMO-1）  
**工时预算**：3.0 人周（120h；5/13–6/15）  
**关闭 finding**：G P0-G-1(b) · G P1-G-4 · G P1-G-5 · P2-E1

---

## §0 设计决策记录（brainstorming 锁定，D1.3b.1 内不再议）

| 决策 | 内容 | Rationale |
|---|---|---|
| **YAML 格式** | maritime-schema `TrafficSituation` v0.2.1（pip `maritime-schema==0.0.7`）+ FCB `metadata.*` 扩展；所有 32 个场景统一采用此格式 | CCS 审计面对 WGS84 标准格式，可追溯至 DNV maritim-schema 规范 |
| **ScenarioSpec 迁移策略（B1）** | `ScenarioSpec.from_file()` 工厂方法负责解析 maritime-schema YAML + lat/lon→ENU 转换；`simulate.py` 内核 ENU 不变 | 职责在 domain model；无外部 adapter 文件；seam 可见且可测 |
| **地理坐标解耦** | 遭遇几何（相对 ΔN/ΔE + COG/SOG）是 SHA256 冻结的不变量；`geo_origin`（lat/lon 锚点）是运行时参数，默认 Norwegian Sea anchor (63.0°N, 5.0°E) | 同一几何可在不同 ENC 区域复用；CCS 覆盖 ODD-B/C 时注入峡湾 origin |
| **Imazu-22 基准来源** | `/Users/marine/Code/colav-simulator/scenarios/imazu_cases/` NTNU colav-simulator（Tengesdal & Johansen 2023 CCTA）；22 个 YAML 反算相对几何 | 论文可引用的公开基准；几何参数来自 UTM zone 33 坐标系反算 |
| **SHA256 冻结** | `scenarios/imazu22/MANIFEST.sha256` 记录 22 个文件各自 SHA256 + 集合哈希；CI gate `tools/ci/check-imazu22-hash.sh` 每次 PR 执行 | 证据链不可篡改；CCS 认证资产冻结 |
| **Cerberus 双语言验证** | Python `cerberus` + C++ `cerberus-cpp`（dokempf/cerberus-cpp）共享 `tools/sil/cerberus_schema/fcb_scenario_v2.yaml`；schema 仅使用 cerberus-cpp 兼容子集（type/required/min/max/allowed/schema） | 同一 schema 文件跨语言；cerberus-cpp 不支持 allof/check_with/coerce，schema 设计绕开 |
| **Arrow 双输出** | batch_runner 同时写 `batch_summary.arrow`（32 行）+ `batch_trajectories.arrow`（约 32 万行）；JSON 输出保留 | D2.5 replay 消费 trajectories；D3.6 Monte Carlo 消费 summary；两文件 schema 独立解耦 |
| **覆盖率维度** | 32 场景覆盖 Rule 13/14/15/16（基础）；ODD-A 全部；Rule 5/6/7/8/9/17/19 标注"待 D1.6 扩展" | D1.3b.1 阶段不扩展未覆盖 Rule；D1.6 升 schema v3.0 加追溯字段 |
| **Own-ship 物理** | 完整 MMG C++ `rk4_step()`（pybind11 绑定）；target ship 直线匀速外推 | 与 D1.3.1 Qualification Report 物理基线一致 |
| **批量执行器语义** | 双 pass（no-action + with-action）；pass/fail = 场景几何合规性验证，非 M1/M6 决策正确性 | D1.3b 阶段 M1/M6 仅 stub；决策验证在 D2.4 |
| **ROS2 版本** | Ubuntu 22.04 + ROS2 **Humble**（非 Jazzy）；pybind11 通过 `find_package(Python3)` + `pybind11_add_module` 集成 | SIL decisions doc §3 锁定 |

---

## §1 文件结构

### 1.1 新增文件

```
scenarios/
├── colregs/                                        ← 已有（保留，迁移到 v2.0 格式）
│   ├── schema.yaml                                 ← UPDATE: 更新为 v2.0 格式说明
│   ├── colreg-rule14-ho-001-seed42-v1.0.yaml       ← 迁移到 maritime-schema v2.0（×10）
│   └── ...
└── imazu22/                                        ← NEW: Imazu-22 场景目录
    ├── imazu-01-ho-v1.0.yaml                       ← NEW（×22，见 §6）
    ├── ...
    ├── imazu-22-ms-v1.0.yaml
    └── MANIFEST.sha256                             ← NEW: SHA256 冻结清单

tools/
└── sil/
    ├── scenario_spec.py                            ← 修改: +from_file() + v2.0 解析
    ├── geo_utils.py                                ← NEW: lat/lon ↔ ENU flat-earth
    ├── batch_runner.py                             ← 修改: 32 场景 + Arrow 输出
    ├── coverage_reporter.py                        ← 修改: 32 场景动态 rule 推断
    ├── cerberus_schema/
    │   └── fcb_scenario_v2.yaml                   ← NEW: cerberus/cerberus-cpp 共享 schema
    ├── cerberus_validator.py                       ← NEW: Python cerberus 验证器
    └── cpp/
        ├── cerberus_validator.cpp                  ← NEW: C++ cerberus-cpp 验证器
        └── CMakeLists.txt                          ← NEW: FetchContent cerberus-cpp

ci/
└── check-imazu22-hash.sh                          ← NEW: CI SHA256 gate

reports/                                            ← 运行时产物（.gitignore）
├── batch_summary.arrow
├── batch_trajectories.arrow
├── batch_results.json
└── coverage_report_<timestamp>.html
```

### 1.2 修改文件

```
tools/sil/scenario_spec.py          ← from_file() 工厂方法 + v1.0 向后兼容
tools/sil/batch_runner.py           ← 动态场景发现（32 个）+ Arrow 写出 + 进度回调
tools/sil/coverage_reporter.py      ← 动态 rule label 推断 + 32 场景矩阵
```

### 1.3 不改动文件（回归边界）

以下 10 个测试文件**禁止修改**：

```
src/l3_tdl_kernel/m8_hmi_transparency_bridge/python/tests/
├── test_app.py
├── test_schemas.py
├── test_tor_endpoint.py
├── test_websocket.py
├── test_sil_hmi.py              ← 现有 23 个测试必须全部 PASS
├── test_ros_bridge_sil.py
├── test_sil_app_integration.py
├── test_sil_router_unit.py
├── test_sil_schemas_unit.py
└── test_sil_ws_unit.py
```

---

## §2 YAML Schema v2.0（maritime-schema + FCB metadata 扩展）

### 2.1 格式规范

所有 32 个场景 YAML 遵循以下结构。maritime-schema `TrafficSituation` 部分（`title` / `own_ship` / `target_ships`）使用 WGS84 lat/lon；FCB 专有字段集中在 `metadata.*`。

```yaml
# ── maritime-schema TrafficSituation v0.2.1 标准字段 ──────────────────────
title: "Head-On Encounter — Imazu Case 01"
description: "Two vessels approaching head-on; OS give-way Rule 14"
start_time: "2026-01-01T00:00:00Z"

own_ship:
  id: "os"
  nav_status: 0        # 0 = underway using engine
  mmsi: 123456789
  initial:
    position:
      latitude: 63.000000     # geo_origin 锚点；by default Norwegian Sea anchor
      longitude: 5.000000
    cog: 0.0                  # Course Over Ground，航海惯例度（CW from North）
    sog: 12.0                 # Speed Over Ground（knots）
    heading: 0.0              # 同 cog（无横漂时）

target_ships:
  - id: "ts1"
    nav_status: 0
    mmsi: 100000001
    initial:
      position:
        latitude: 63.117318   # OS + ΔN=+13060m
        longitude: 5.000000
      cog: 180.0
      sog: 10.0
      heading: 180.0

# ── FCB metadata 扩展字段（additionalProperties: true）─────────────────────
metadata:
  schema_version: "2.0"
  scenario_id: "imazu-01-ho-v1.0"       # 命名规范：imazu-NN-{type}-v{ver}
  scenario_source: "imazu1987"           # "imazu1987" | "fcb_original" | "custom"
  vessel_class: "FCB"
  odd_zone: "A"                          # "A" | "B" | "C"

  geo_origin:
    latitude: 63.0
    longitude: 5.0
    description: "Norwegian Sea anchor (default); override per ENC region"

  encounter:
    rule: "Rule14"                       # "Rule13"|"Rule14"|"Rule15"|"Rule16"
    give_way_vessel: "own"              # "own"|"target"|"none"
    expected_own_action: "turn_starboard"
    avoidance_time_s: 300.0
    avoidance_delta_rad: 0.6109         # +35° starboard
    avoidance_duration_s: 90.0

  disturbance_model:
    wind_kn: 0.0
    wind_dir_nav_deg: 0.0
    current_kn: 0.0
    current_dir_nav_deg: 0.0
    vis_m: 10000.0
    wave_height_m: 0.0

  pass_criteria:
    max_dcpa_no_action_m: 926.0         # 0.5 nm；无动作时须 < 此值（确认存在风险）
    min_dcpa_with_action_m: 926.0       # 有动作时须 ≥ 此值（确认可解）

  simulation:
    duration_s: 600.0
    dt_s: 0.02
    n_rps_initial: 3.5                  # 维持初速对应螺旋桨转速（rev/s）

  prng_seed: null                       # Imazu 场景确定性，无随机化
```

### 2.2 schema_version 版本语义

| `metadata.schema_version` | 格式 | from_file() 处理 |
|---|---|---|
| `"1.0"` | 旧 ENU 格式（`initial_conditions.own_ship.x_m/y_m`）| 直接 `model_validate()`，向后兼容 |
| `"2.0"` | maritime-schema WGS84 格式（本规范）| lat/lon → ENU 转换后构造 ScenarioSpec |

### 2.3 cerberus-cpp 兼容子集约束

以下字段类型**禁止**出现在 cerberus schema（cerberus-cpp 不支持）：
- `allof` / `anyof` / `oneof`
- `check_with`（自定义验证器）
- `coerce`（类型强制转换）
- `readonly` / `dependencies`

所有验证逻辑须通过 `type` + `required` + `min`/`max` + `allowed` + `schema`（嵌套）表达。

---

## §3 geo_utils.py — Flat-Earth 坐标桥接

### 3.1 接口

```python
# tools/sil/geo_utils.py
"""Flat-earth WGS84 ↔ ENU conversion. Valid for offsets < 50 km (error < 0.5 m)."""
from __future__ import annotations
import math

_R_EARTH_M = 6_371_000.0
_DEFAULT_GEO_ORIGIN = (63.0, 5.0)   # Norwegian Sea anchor


def latlon_to_enu(
    lat_origin_deg: float,
    lon_origin_deg: float,
    lat_deg: float,
    lon_deg: float,
) -> tuple[float, float]:
    """Return (north_m, east_m) relative to origin."""
    lat_origin_rad = math.radians(lat_origin_deg)
    north_m = (lat_deg - lat_origin_deg) * math.radians(1) * _R_EARTH_M
    east_m = (lon_deg - lon_origin_deg) * math.radians(1) * _R_EARTH_M * math.cos(lat_origin_rad)
    return north_m, east_m


def enu_to_latlon(
    lat_origin_deg: float,
    lon_origin_deg: float,
    north_m: float,
    east_m: float,
) -> tuple[float, float]:
    """Return (lat_deg, lon_deg) from ENU offset relative to origin."""
    lat_origin_rad = math.radians(lat_origin_deg)
    lat_deg = lat_origin_deg + math.degrees(north_m / _R_EARTH_M)
    lon_deg = lon_origin_deg + math.degrees(east_m / (_R_EARTH_M * math.cos(lat_origin_rad)))
    return lat_deg, lon_deg


def default_origin() -> tuple[float, float]:
    return _DEFAULT_GEO_ORIGIN
```

### 3.2 精度保证

| 偏移距离 | north_m 误差 | east_m 误差（@63°N）|
|---|---|---|
| 1 km | < 0.01 m | < 0.05 m |
| 14 km（Imazu-01 最大）| < 0.2 m | < 0.7 m |
| 50 km | < 2.5 m | < 9 m |

Imazu-22 最大偏移 ~14 km，误差 < 1 m，满足 SIL 精度要求。

### 3.3 round-trip 验证（T_GEO 验收条件）

```python
def test_roundtrip():
    origin = (63.0, 5.0)
    for dn, de in [(13060, 0), (7060, 7000), (-5500, 2560)]:
        lat, lon = enu_to_latlon(*origin, dn, de)
        n2, e2 = latlon_to_enu(*origin, lat, lon)
        assert abs(n2 - dn) < 1.0   # < 1 m
        assert abs(e2 - de) < 1.0
```

---

## §4 ScenarioSpec.from_file() — B1 Domain Model Migration

### 4.1 设计原则

`ScenarioSpec` 是 domain model，对自身的实例化方式负责。外部无 adapter 文件；seam 在 `from_file()` 工厂方法内。

### 4.2 from_file() 接口与实现

```python
# tools/sil/scenario_spec.py — 新增方法（现有字段定义不变）

from __future__ import annotations
from pathlib import Path
from typing import Optional
import math, yaml

# ... 现有 Pydantic 类定义（OwnShip, Target, InitialConditions, 等）保持不变 ...


class ScenarioSpec(BaseModel):
    # ... 现有字段 ...

    @classmethod
    def from_file(
        cls,
        path: Path,
        geo_origin: Optional[tuple[float, float]] = None,
    ) -> "ScenarioSpec":
        """Load maritime-schema v2.0 YAML (or legacy v1.0 ENU YAML) into ScenarioSpec.

        geo_origin: (lat_deg, lon_deg) runtime override.
                    None → read from metadata.geo_origin → fallback to (63.0, 5.0).
        """
        from tools.sil.geo_utils import latlon_to_enu, default_origin
        from tools.sil.cerberus_validator import validate_yaml

        data = yaml.safe_load(path.read_text(encoding="utf-8"))
        version = data.get("metadata", {}).get("schema_version", "1.0")

        if version == "1.0":
            # 向后兼容：直接解析 ENU 格式
            return cls.model_validate(data)

        # ── v2.0: maritime-schema parse ──────────────────────────────────────
        validate_yaml(data)   # cerberus validation; raises on failure

        meta = data["metadata"]
        _meta_origin = meta.get("geo_origin", {})
        origin = geo_origin or (
            (_meta_origin["latitude"], _meta_origin["longitude"])
            if _meta_origin else default_origin()
        )

        # Own ship
        own_init = data["own_ship"]["initial"]
        os_pos = own_init["position"]
        os_n, os_e = latlon_to_enu(*origin, os_pos["latitude"], os_pos["longitude"])

        # Target ships
        targets: list[Target] = []
        for i, ts in enumerate(data.get("target_ships", []), start=1):
            ti = ts["initial"]
            ts_pos = ti["position"]
            ts_n, ts_e = latlon_to_enu(*origin, ts_pos["latitude"], ts_pos["longitude"])
            targets.append(Target(
                target_id=i,
                x_m=ts_e,           # ENU convention: x = East
                y_m=ts_n,           # ENU convention: y = North
                cog_nav_deg=ti["cog"],
                sog_mps=ti["sog"] * 0.5144,
            ))

        sim_meta = meta.get("simulation", {})
        enc_meta = meta.get("encounter", {})

        return cls(
            schema_version=meta["schema_version"],
            scenario_id=meta["scenario_id"],
            vessel_class=meta.get("vessel_class", "FCB"),
            odd_zone=meta.get("odd_zone", "A"),
            initial_conditions=InitialConditions(
                own_ship=OwnShip(
                    x_m=os_e,
                    y_m=os_n,
                    heading_nav_deg=own_init.get("heading", own_init["cog"]),
                    speed_kn=own_init["sog"],
                    n_rps=sim_meta.get("n_rps_initial", _speed_to_n_rps(own_init["sog"])),
                ),
                targets=targets,
            ),
            encounter=Encounter(**enc_meta) if enc_meta else None,
            disturbance_model=DisturbanceModel(**meta.get("disturbance_model", {})),
            pass_criteria=PassCriteria(**meta["pass_criteria"]) if "pass_criteria" in meta else None,
            simulation=SimulationConfig(
                duration_s=sim_meta.get("duration_s", 600.0),
                dt_s=sim_meta.get("dt_s", 0.02),
            ),
            prng_seed=meta.get("prng_seed"),
        )


def _speed_to_n_rps(speed_kn: float) -> float:
    """Approximate n_rps from speed using FCB calibration table."""
    table = [(8, 2.3), (10, 3.0), (12, 3.5), (14, 4.2), (15, 4.5)]
    for spd, n in table:
        if speed_kn <= spd:
            return n
    return 4.5
```

### 4.3 ENU 坐标系约定

`ScenarioSpec` 内部统一 ENU：
- `x_m` = East（米）
- `y_m` = North（米）
- maritime-schema YAML 中 `position.latitude` → `y_m`（North），`position.longitude` → `x_m`（East）
- `psi_math_rad = π/2 - radians(heading_nav_deg)`（内核转换，发生在 `simulate.py` 调用前）

---

## §5 Cerberus 双语言验证

### 5.1 cerberus schema YAML（Python + C++ 共享）

```yaml
# tools/sil/cerberus_schema/fcb_scenario_v2.yaml
# cerberus-cpp 兼容子集：仅使用 type/required/min/max/allowed/schema

title:
  type: string
  required: true

own_ship:
  type: dict
  required: true
  schema:
    initial:
      type: dict
      required: true
      schema:
        position:
          type: dict
          required: true
          schema:
            latitude: {type: float, required: true, min: -90.0, max: 90.0}
            longitude: {type: float, required: true, min: -180.0, max: 180.0}
        cog: {type: float, required: true, min: 0.0, max: 360.0}
        sog: {type: float, required: true, min: 0.0, max: 50.0}

target_ships:
  type: list
  required: false
  schema:
    type: dict
    schema:
      id: {type: string, required: true}
      initial:
        type: dict
        required: true
        schema:
          position:
            type: dict
            required: true
            schema:
              latitude: {type: float, required: true, min: -90.0, max: 90.0}
              longitude: {type: float, required: true, min: -180.0, max: 180.0}
          cog: {type: float, required: true, min: 0.0, max: 360.0}
          sog: {type: float, required: true, min: 0.0, max: 50.0}

metadata:
  type: dict
  required: true
  schema:
    schema_version: {type: string, required: true, allowed: ["2.0"]}
    scenario_id: {type: string, required: true}
    odd_zone: {type: string, required: true, allowed: [A, B, C]}
    vessel_class: {type: string, required: true}
    pass_criteria:
      type: dict
      required: true
      schema:
        max_dcpa_no_action_m: {type: float, required: true, min: 0.0}
        min_dcpa_with_action_m: {type: float, required: true, min: 0.0}
    simulation:
      type: dict
      required: true
      schema:
        duration_s: {type: float, required: true, min: 600.0}
        dt_s: {type: float, required: true, min: 0.01, max: 0.1}
```

### 5.2 Python cerberus_validator.py

```python
# tools/sil/cerberus_validator.py
from pathlib import Path
import cerberus
import yaml

_SCHEMA_PATH = Path(__file__).parent / "cerberus_schema" / "fcb_scenario_v2.yaml"


def validate_yaml(data: dict) -> None:
    """Validate maritime-schema v2.0 data dict. Raises ValueError on failure."""
    schema = yaml.safe_load(_SCHEMA_PATH.read_text(encoding="utf-8"))
    v = cerberus.Validator(schema, allow_unknown=True)   # allow_unknown for additionalProperties
    if not v.validate(data):
        raise ValueError(f"Schema validation failed: {v.errors}")
```

### 5.3 C++ cerberus-cpp 验证器

```cmake
# tools/sil/cpp/CMakeLists.txt
cmake_minimum_required(VERSION 3.11)
project(fcb_cerberus_validator)

include(FetchContent)
FetchContent_Declare(
  cerberus_cpp
  GIT_REPOSITORY https://github.com/dokempf/cerberus-cpp.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(cerberus_cpp)

find_package(yaml-cpp REQUIRED)

add_executable(validate_scenario
  cerberus_validator.cpp
)
target_include_directories(validate_scenario PRIVATE ${cerberus_cpp_SOURCE_DIR}/include)
target_link_libraries(validate_scenario PRIVATE yaml-cpp)
```

```cpp
// tools/sil/cpp/cerberus_validator.cpp
// Usage: ./validate_scenario <schema.yaml> <scenario.yaml>
// Exit 0 = valid, Exit 1 = invalid (errors to stderr)
#include "cerberus-cpp/cerberus.hpp"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: validate_scenario <schema.yaml> <scenario.yaml>\n";
        return 2;
    }
    auto schema   = YAML::LoadFile(argv[1]);
    auto document = YAML::LoadFile(argv[2]);

    cerberus::Validator validator(schema);
    if (validator.validate(document)) {
        return 0;
    }
    for (const auto& err : validator.errors()) {
        std::cerr << err << "\n";
    }
    return 1;
}
```

### 5.4 cerberus-cpp 已知不兼容项

以下 Python cerberus 特性在 cerberus-cpp 中**不可用**，schema 设计已绕开：

| 特性 | cerberus-cpp 支持 | 绕开方式 |
|---|---|---|
| `allof` / `anyof` | ❌ | 拆分为独立字段验证 |
| `check_with`（自定义函数）| ❌ | 移至 `ScenarioSpec` Pydantic validator |
| `coerce`（类型转换）| ❌ | YAML 源文件须显式写正确类型 |
| `allowed` | ✅ | 用于 odd_zone/schema_version 枚举 |
| `min` / `max` | ✅ | 用于坐标和速度范围 |

---

## §6 Imazu-22 场景参数

### 6.1 几何来源

以 NTNU colav-simulator（UTM zone 33，`map_origin_enu=[267492.6, 7041883.4]`）的 `csog_state: [N_utm, E_utm, speed_kn, cog_deg]` 反算 OS→TS 相对偏移（ΔN_m, ΔE_m）。maritime-schema YAML 使用 geo_origin 锚点 (63.0°N, 5.0°E) 加 ΔN/ΔE 转换为 lat/lon。

### 6.2 完整几何参数表

OS 始终位于 geo_origin (63.0°N, 5.0°E) / (x_m=0, y_m=0, heading=0°, sog=10 kn)。

| ID | scenario_id | Type | TS1 ΔN/ΔE (m) | TS1 COG° / SOG kn | TS2 ΔN/ΔE (m) | TS2 COG°/SOG | TS3 ΔN/ΔE (m) | TS3 COG°/SOG | duration_s |
|---|---|---|---|---|---|---|---|---|---|
| 01 | imazu-01-ho-v1.0 | HO | +13060 / 0 | 180 / 10 | — | — | — | — | 700 |
| 02 | imazu-02-cr-gw-v1.0 | CR_GW | +7060 / +7000 | 270 / 10 | — | — | — | — | 1000 |
| 03 | imazu-03-ot-v1.0 | OT | +2060 / 0 | 0 / 5 | — | — | — | — | 1000 |
| 04 | imazu-04-cr-so-v1.0 | CR_SO | +2560 / -5500 | 40 / 10 | — | — | — | — | 1000 |
| 05 | imazu-05-ms-v1.0 | MS | +7060 / +7000 | 270 / 10 | +14120 / 0 | 180 / 10 | — | — | 1000 |
| 06 | imazu-06-ms-v1.0 | MS | +2560 / +5000 | 320 / 10 | +860 / +2700 | 342 / 10 | — | — | 1000 |
| 07 | imazu-07-ms-v1.0 | MS | +2560 / +5000 | 320 / 9 | +2060 / 0 | 0 / 5 | — | — | 1000 |
| 08 | imazu-08-ms-v1.0 | MS | +7060 / -7000 | 90 / 10 | +14120 / 0 | 180 / 10 | — | — | 1000 |
| 09 | imazu-09-ms-v1.0 | MS | +7060 / +7000 | 270 / 10 | +810 / +2950 | 345 / 10 | — | — | 1000 |
| 10 | imazu-10-ms-v1.0 | MS | +7060 / +7000 | 270 / 10 | +710 / -2750 | 45 / 10 | — | — | 1000 |
| 11 | imazu-11-ms-v1.0 | MS | +7060 / -7000 | 90 / 10 | +810 / +2950 | 345 / 10 | — | — | 1000 |
| 12 | imazu-12-ms-v1.0 | MS | +2200 / +5000 | 320 / 9 | +350 / -2750 | 45 / 10 | +13760 / 0 | 180 / 10 | 1000 |
| 13 | imazu-13-ms-v1.0 | MS | +2200 / -5500 | 40 / 10 | +350 / -2750 | 45 / 10 | +13760 / 0 | 180 / 10 | 1000 |
| 14 | imazu-14-ms-v1.0 | MS | +2200 / +5000 | 320 / 10 | +500 / +2700 | 342 / 10 | +5700 / +6400 | 270 / 10 | 1000 |
| 15 | imazu-15-ms-v1.0 | MS | +2200 / +5000 | 320 / 10 | +1700 / 0 | 0 / 5 | +5700 / +6400 | 270 / 10 | 1000 |
| 16 | imazu-16-ms-v1.0 | MS | +6700 / -7000 | 90 / 10 | +350 / -2750 | 45 / 10 | +6700 / +7000 | 270 / 10 | 1000 |
| 17 | imazu-17-ms-v1.0 | MS | +2200 / +5000 | 320 / 10 | +1700 / 0 | 0 / 5 | +350 / -2750 | 45 / 10 | 1000 |
| 18 | imazu-18-ms-v1.0 | MS | +2200 / +5000 | 320 / 10 | +500 / +2700 | 342 / 10 | +10200 / +4500 | 225 / 10 | 1000 |
| 19 | imazu-19-ms-v1.0 | MS | +350 / -2450 | 45 / 10 | +500 / +2700 | 342 / 10 | +10200 / +4500 | 225 / 10 | 1000 |
| 20 | imazu-20-ms-v1.0 | MS | +500 / +2700 | 342 / 10 | +1700 / 0 | 0 / 5 | +5700 / +6400 | 270 / 10 | 1000 |
| 21 | imazu-21-ms-v1.0 | MS | +350 / -2450 | 45 / 10 | +500 / +2700 | 342 / 10 | +5700 / +6400 | 270 / 10 | 1000 |
| 22 | imazu-22-ms-v1.0 | MS | +2200 / +5000 | 320 / 10 | +1700 / 0 | 0 / 5 | +5700 / +6400 | 270 / 10 | 1000 |

> Cases 22 和 15 几何相同（来自 colav-simulator 原始文件）；作为独立场景保留，scenario_id 区分。

### 6.3 lat/lon 计算公式

对表中每条 ΔN/ΔE，使用 geo_utils.enu_to_latlon(63.0, 5.0, ΔN, ΔE) 计算 lat/lon 写入 YAML。实现时直接调用 `geo_utils.py` 生成；不手算写死。提供脚本：

```bash
# tools/sil/generate_imazu_yaml.py
# 遍历 §6.2 参数表 → 输出 scenarios/imazu22/imazu-NN-*.yaml
```

### 6.4 pass_criteria 默认值

所有 Imazu-22 场景：
- `max_dcpa_no_action_m: 926.0`（0.5 nm；Imazu 基准场景均设计为碰撞威胁）
- `min_dcpa_with_action_m: 500.0`（有动作后的安全间距；Multi-Ship 场景设为 300.0）
- `avoidance_time_s`：按 0.55 × TCPA 估算（实现时先用 300s 统一值，T_IMAZU 验收时根据实际 TCPA 调整）

### 6.5 disturbance_model

所有 Imazu-22 场景 disturbance_model 全为零值（原论文基准场景无扰动）。

---

## §7 SHA256 冻结 + CI Gate

### 7.1 MANIFEST.sha256 格式

```
# scenarios/imazu22/MANIFEST.sha256
# 每行：<sha256hex>  <filename>
# 集合哈希（最后一行，由所有文件哈希的有序拼接计算）
abc123...  imazu-01-ho-v1.0.yaml
def456...  imazu-02-cr-gw-v1.0.yaml
...
xyz789...  imazu-22-ms-v1.0.yaml
COLLECTION_SHA256: <sha256_of_sorted_concatenation>
```

### 7.2 CI gate 脚本

```bash
#!/bin/bash
# tools/ci/check-imazu22-hash.sh
# 用途：CI PR gate；若任何 Imazu-22 文件被修改则失败

set -euo pipefail

MANIFEST="scenarios/imazu22/MANIFEST.sha256"
SCENARIOS_DIR="scenarios/imazu22"

# 验证每个文件的哈希
while IFS='  ' read -r expected_hash filename; do
  [[ "$filename" == COLLECTION_SHA256:* ]] && continue
  actual_hash=$(sha256sum "$SCENARIOS_DIR/$filename" | awk '{print $1}')
  if [[ "$actual_hash" != "$expected_hash" ]]; then
    echo "FAIL: $filename hash mismatch"
    echo "  Expected: $expected_hash"
    echo "  Actual:   $actual_hash"
    exit 1
  fi
done < "$MANIFEST"

echo "OK: All 22 Imazu scenario files match MANIFEST.sha256"
```

### 7.3 MANIFEST 生成（仅首次，提交后只读）

```bash
# tools/ci/generate-imazu22-manifest.sh（仅运行一次，结果 commit 到 git）
cd scenarios/imazu22
for f in imazu-*.yaml; do
  printf "$(sha256sum "$f" | awk '{print $1}')  $f\n"
done | sort > MANIFEST.sha256
# 追加 COLLECTION_SHA256
cat $(ls imazu-*.yaml | sort) | sha256sum | awk '{print "COLLECTION_SHA256: "$1}' >> MANIFEST.sha256
```

### 7.4 CI 集成

在 `.gitlab-ci.yml`（或 GitHub Actions）的 `test` stage 添加：

```yaml
check-imazu22-hash:
  stage: test
  script:
    - bash tools/ci/check-imazu22-hash.sh
```

---

## §8 现有 10 个场景迁移至 v2.0

### 8.1 迁移策略

10 个 `scenarios/colregs/colreg-*.yaml` 文件**原地重写**（保留文件名，`scenario_id` 字段不变）：
- `schema_version: "1.0"` ENU 格式 → `metadata.schema_version: "2.0"` WGS84 格式
- 使用 `geo_utils.enu_to_latlon(63.0, 5.0, y_m, x_m)` 将原 ENU 坐标转回 lat/lon
- 所有其他字段（encounter / disturbance_model / pass_criteria / simulation）迁移至 `metadata.*`
- `ScenarioSpec.from_file()` 向后兼容 v1.0 格式；迁移完成后 v1.0 代码路径仍保留但仅用于测试

### 8.2 迁移脚本

```bash
# tools/sil/migrate_v1_to_v2.py
# 读取 scenarios/colregs/*.yaml (v1.0) → 转换 → 覆盖写回 (v2.0)
# 运行前做 git diff 备份
```

---

## §9 batch_runner.py 扩展

### 9.1 动态场景发现

```python
def discover_scenarios(dirs: list[Path]) -> list[Path]:
    """Glob all *.yaml files from given dirs, excluding schema.yaml."""
    results = []
    for d in dirs:
        results.extend(sorted(
            f for f in d.glob("*.yaml") if f.name != "schema.yaml"
        ))
    return results
```

默认 dirs：`[Path("scenarios/colregs"), Path("scenarios/imazu22")]`，共 32 个场景。

### 9.2 批量执行主流程

```python
def run_batch(
    scenario_dirs: list[Path],
    output_dir: Path,
    geo_origin: tuple[float, float] | None = None,
    progress_cb: Callable[[int, int], None] | None = None,
) -> dict:
    """Run all scenarios. Returns batch summary dict."""
    scenarios = discover_scenarios(scenario_dirs)
    results = []
    for i, yaml_path in enumerate(scenarios):
        spec = ScenarioSpec.from_file(yaml_path, geo_origin=geo_origin)
        result = _run_single_scenario(spec, output_dir)
        results.append(result)
        if progress_cb:
            progress_cb(i + 1, len(scenarios))

    _write_json_summary(results, output_dir / "batch_results.json")
    _write_arrow_summary(results, output_dir / "batch_summary.arrow")
    _write_arrow_trajectories(results, output_dir / "batch_trajectories.arrow")
    return {"n_scenarios": len(scenarios), "n_pass": sum(r["pass"] for r in results)}
```

### 9.3 进度回调（与 sil_router.py 集成）

`_run_batch_job()` 在 `sil_router.py` 中通过 `progress_cb` 更新 `_jobs[job_id]["progress"]`，无需修改 sil_router.py 接口。

---

## §10 Arrow 输出规范

### 10.1 batch_summary.arrow schema

```python
import pyarrow as pa

SUMMARY_SCHEMA = pa.schema([
    pa.field("scenario_id",          pa.string()),
    pa.field("rule",                 pa.string()),
    pa.field("odd_zone",             pa.string()),
    pa.field("dcpa_no_action_m",     pa.float32()),
    pa.field("dcpa_with_action_m",   pa.float32()),
    pa.field("min_separation_m",     pa.float32()),
    pa.field("pass",                 pa.bool_()),
    pa.field("duration_s",           pa.float32()),
    pa.field("wall_clock_s",         pa.float32()),
    pa.field("scenario_source",      pa.string()),  # "imazu1987" | "fcb_original"
])
```

### 10.2 batch_trajectories.arrow schema

```python
TRAJECTORY_SCHEMA = pa.schema([
    pa.field("scenario_id",  pa.string()),
    pa.field("t_s",          pa.float32()),
    pa.field("os_x_m",       pa.float32()),
    pa.field("os_y_m",       pa.float32()),
    pa.field("ts1_x_m",      pa.float32()),   # nullable
    pa.field("ts1_y_m",      pa.float32()),
    pa.field("ts2_x_m",      pa.float32()),   # nullable (null if < 2 targets)
    pa.field("ts2_y_m",      pa.float32()),
    pa.field("ts3_x_m",      pa.float32()),   # nullable (null if < 3 targets)
    pa.field("ts3_y_m",      pa.float32()),
])
```

### 10.3 軌跡采样

每 5 个仿真步（Δt=0.1s）记录一行。1000s 场景 → 10,000 行；600s 场景 → 6,000 行。32 场景总计约 ~300,000 行，~30 MB。

```python
TRAJECTORY_SAMPLE_INTERVAL = 5   # steps
```

### 10.4 写出方式

```python
import pyarrow as pa
import pyarrow.ipc as ipc

def _write_arrow_trajectories(all_traj_rows: list[dict], path: Path) -> None:
    table = pa.table(all_traj_rows, schema=TRAJECTORY_SCHEMA)
    with pa.OSFile(str(path), "wb") as sink:
        with ipc.new_file(sink, TRAJECTORY_SCHEMA) as writer:
            writer.write_table(table)
```

---

## §11 JSON 输出格式（保留）

每场景运行后生成 `reports/<scenario_id>-<timestamp>.json`：

```json
{
  "schema_version": "2.0",
  "scenario_id": "imazu-01-ho-v1.0",
  "scenario_yaml": "scenarios/imazu22/imazu-01-ho-v1.0.yaml",
  "run_timestamp": "2026-06-01T09:00:00Z",
  "result": "PASS",
  "sub_checks": {
    "geometric_compliance": true,
    "bearing_sector": true,
    "solvability": true,
    "stability": true,
    "wall_clock_le_60s": true
  },
  "metrics": {
    "dcpa_no_action_m": 12.4,
    "dcpa_with_action_m": 1124.7,
    "tcpa_no_action_s": 327.2,
    "min_separation_m": 1124.7,
    "wall_clock_s": 0.31
  },
  "scenario_source": "imazu1987",
  "trajectory_points": 300
}
```

---

## §12 coverage_reporter.py 扩展

### 12.1 动态 rule 推断

```python
def _infer_rule_label(scenario_id: str, metadata: dict) -> str:
    """Infer COLREGs rule from scenario metadata.encounter.rule, fallback to id pattern."""
    enc = metadata.get("encounter", {})
    rule = enc.get("rule", "")
    if rule:
        return rule
    # fallback: id pattern matching (legacy v1.0 scenarios)
    if "ho" in scenario_id:
        return "Rule14"
    if "cr" in scenario_id:
        return "Rule15"
    if "ot" in scenario_id:
        return "Rule13"
    return "Unknown"
```

### 12.2 HTML 报告矩阵维度

32 场景覆盖行：

| Rule | 场景数 |
|---|---|
| Rule 14 Head-on | 3（原有）+ 1（Imazu-01）= 4 |
| Rule 15 Crossing GW | 4（原有）+ 1（Imazu-02）= 5 |
| Rule 13 Overtaking | 3（原有）+ 1（Imazu-03）= 4 |
| Rule 15 Crossing SO | 0（原有）+ 1（Imazu-04）= 1 |
| Multi-Ship | 0（原有）+ 18（Imazu-05..22）= 18 |
| 未覆盖 Rule 5/6/7/8/9/17/19 | ⚠️ D1.6 扩展 |

---

## §13 pybind11 绑定（保留 v1.0 §3，修正 ROS2 版本）

### 13.1 暴露接口（同 v1.0）

（见 v1.0 §3.1 — 接口不变）

### 13.2 坐标系约定（同 v1.0）

`psi_math_rad = π/2 - radians(heading_nav_deg)`

### 13.3 CMakeLists.txt — ROS2 Humble 修正

```cmake
# ROS2 Humble（非 Jazzy）的 pybind11 集成方式：
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)
# 注：Humble 不提供 pybind11_vendor；通过 apt install python3-pybind11 或 pip install pybind11

pybind11_add_module(fcb_sim_py
  python/fcb_sim_py/bindings.cpp
)
target_link_libraries(fcb_sim_py PRIVATE fcb_simulator_core)
install(TARGETS fcb_sim_py
  LIBRARY DESTINATION ${PYTHON_INSTALL_DIR})
```

---

## §14 SIL Mock Publisher（保留 v1.0 §9）

（接口与 topic 名同 v1.0；D2.5 切换路径同 v1.0）

---

## §15 SIL HMI REST + WebSocket（保留 v1.0 §10）

`sil_router.py` 无接口变更。`run_batch()` 签名从接受单个 `SCENARIOS_DIR: Path` 扩展为接受 `list[Path]`：

```python
# sil_router.py 中调用变更（仅此一行）：
batch_runner.run_batch(
    [SCENARIOS_DIR, IMAZU22_DIR],   # ← 从单 Path 改为 list
    REPORTS_DIR
)
```

新增常量：

```python
IMAZU22_DIR = Path("scenarios/imazu22")
```

---

## §16 Task 拆分（共 28 tasks，约 110h）

### Track A：pybind11 绑定（ROS2 Humble）

| ID | Task | 工时 |
|---|---|---|
| T-A1 | CMakeLists.txt Humble 适配 + `colcon build` 验证 | 4h |
| T-A2 | bindings.cpp FcbState + MmgParams + rk4_step 绑定 + smoke test | 6h |

### Track B：坐标桥接 + ScenarioSpec 迁移

| ID | Task | 工时 |
|---|---|---|
| T-B1 | `geo_utils.py` 实现 + round-trip 单元测试（3 组坐标，误差 < 1m）| 3h |
| T-B2 | `ScenarioSpec.from_file()` v2.0 解析路径 + v1.0 向后兼容路径 | 4h |
| T-B3 | `cerberus_validator.py` + `fcb_scenario_v2.yaml` schema | 3h |
| T-B4 | C++ cerberus-cpp FetchContent CMake 集成 + 编译验证 | 4h |
| T-B5 | `cerberus_validator.cpp` 实现 + CLI 验证 1 个场景 | 3h |

### Track C：场景 YAML 创作

| ID | Task | 工时 |
|---|---|---|
| T-C1 | 10 个原有场景迁移脚本 + 执行迁移（v1.0 → v2.0）+ cerberus 验证通过 | 4h |
| T-C2 | `generate_imazu_yaml.py` 生成脚本 + 生成 Imazu-01..04（HO/CR/OT/CR_SO）| 3h |
| T-C3 | 生成 Imazu-05..11（双船 MS）+ cerberus 验证 | 3h |
| T-C4 | 生成 Imazu-12..22（三船 MS）+ cerberus 验证 | 3h |
| T-C5 | MANIFEST.sha256 生成 + CI gate 脚本 + 本地验证通过 | 2h |

### Track D：批量执行器 + Arrow 输出

| ID | Task | 工时 |
|---|---|---|
| T-D1 | `simulate()` 更新（多目标支持已有，验证 3-target Imazu 场景运行）| 2h |
| T-D2 | `batch_runner.py` 动态场景发现（32 个）+ 进度回调 | 3h |
| T-D3 | Arrow summary 写出（pyarrow）+ 验证 schema | 3h |
| T-D4 | Arrow trajectories 写出（采样 + nullable 字段）+ 验证 schema | 4h |

### Track E：HTML 覆盖率报告

| ID | Task | 工时 |
|---|---|---|
| T-E1 | `coverage_reporter.py` 动态 rule 推断 + 32 场景矩阵渲染 | 4h |
| T-E2 | Jinja2 模板扩展（Multi-Ship 行 + 未覆盖 Rule ⚠️）| 3h |

### Track F：SIL HMI 扩展

| ID | Task | 工时 |
|---|---|---|
| T-F1 | `sil_router.py` 添加 IMAZU22_DIR + list[Path] 调用 | 1h |
| T-F2 | `sil_ws.py` WebSocket + `broadcast_sil_state()` | 3h |
| T-F3 | `sil_schemas.py` Pydantic 模型（SilSAT1Panel / SilODDPanel / SilDebugSchema）| 2h |
| T-F4 | `ros_bridge.py` 扩展（订阅 SAT/ODD topic）+ `app.py` include_router | 3h |
| T-F5 | SIL Mock Publisher ROS2 包（`sil_mock_publisher`）| 4h |

### Track G：集成与验收

| ID | Task | 工时 |
|---|---|---|
| T-G1 | 32 场景全量批量运行 + wall-clock 验证 + JSON/Arrow 输出验证 | 4h |
| T-G2 | C++ cerberus-cpp 对 32 个 YAML 全量验证通过 | 2h |
| T-G3 | 回归测试：10 个测试文件全部 PASS（含 test_sil_hmi.py 23 个）| 3h |
| T-G4 | DEMO-1 端到端预演 + DoD 全闭验证 + finding 关闭文档 | 3h |

**总计：110h（约 2.75 人周）。3.0pw=120h，余量 10h 覆盖 cerberus-cpp 集成风险。**

---

## §17 每 Task Acceptance Criteria

| ID | Acceptance Criteria |
|---|---|
| T-A1 | `colcon build --packages-select fcb_simulator` 成功（Humble）；`python -c "import fcb_sim_py"` 无报错 |
| T-A2 | smoke test 通过：rk4_step 后 u > 0，psi 稳定；wall_clock 0.02s × 30000 步 < 5s |
| T-B1 | `test_roundtrip()` 通过；3 组 ΔN/ΔE 坐标 round-trip 误差 < 1m |
| T-B2 | `ScenarioSpec.from_file(imazu-01.yaml)` 解析成功；OS y_m ≈ 0，TS1 y_m ≈ 13060 |
| T-B3 | cerberus 验证通过 imazu-01；故意缺 `metadata.scenario_id` 时抛出 ValueError |
| T-B4 | `cmake --build` 成功；`./validate_scenario schema.yaml imazu-01.yaml` 退出码 0 |
| T-B5 | `./validate_scenario schema.yaml broken.yaml` 退出码 1 并输出错误字段 |
| T-C1 | 10 个 colregs YAML 通过 cerberus 验证；`ScenarioSpec.from_file()` 对所有 10 个均可解析 |
| T-C2 | imazu-01..04.yaml 存在；cerberus 验证通过；C++ cerberus-cpp 验证通过 |
| T-C3 | imazu-05..11.yaml 存在；3-target 场景 `from_file()` 返回 2 个 targets |
| T-C4 | imazu-12..22.yaml 存在；4-target 场景 `from_file()` 返回 3 个 targets |
| T-C5 | `check-imazu22-hash.sh` 对未修改文件输出 "OK"；修改任一文件后输出 "FAIL" 并退出 1 |
| T-D1 | Imazu-01（2-ship）+ Imazu-12（3-ship）无动作运行无 NaN；dcpa < 926m |
| T-D2 | `discover_scenarios([colregs, imazu22])` 返回 32 个路径；进度回调被调用 32 次 |
| T-D3 | `batch_summary.arrow` 可被 `pa.ipc.open_file()` 读取；schema 匹配 §10.1；32 行 |
| T-D4 | `batch_trajectories.arrow` schema 匹配 §10.2；ts2_x_m 在 2-ship 场景中为 null |
| T-E1 | HTML 报告含 Multi-Ship 行（18 个场景）；Rule 14 行含 4 个场景 |
| T-E2 | 未覆盖 Rule 5/6/7/8/9/17/19 有 ⚠️ "待 D1.6 扩展" 标注可见 |
| T-F1 | `GET /sil/scenario/list` 返回 32 个文件名（含 imazu-*.yaml）|
| T-F2 | `/ws/sil_debug` 连接成功；mock publisher 运行时每 ~1s 收到 SilDebugSchema JSON |
| T-F3 | `SilDebugSchema(timestamp=..., scenario_id="x").model_dump_json()` 可解析 |
| T-F4 | `ros_bridge.latest_sat` 在 mock publisher 运行时不为 None |
| T-F5 | `ros2 topic hz /l3/sat/data` 显示 ~1 Hz |
| T-G1 | 32 场景全量运行；所有 JSON 生成；`batch_summary.arrow` 32 行；max wall_clock_s < 60 |
| T-G2 | C++ `validate_scenario` 对 32 个 YAML 全部退出 0 |
| T-G3 | 10 个测试文件全部 `pytest` PASS；test_sil_hmi.py 23 个测试全绿 |
| T-G4 | DoD §18 全部复选框勾选；finding 关闭文档写入指定路径 |

---

## §18 DEMO-1 Charter（2026-06-15）

**Visible Success（4 项）**：
1. 浏览器 `/ws/sil_debug` 实时推送 SAT + ODD（mock publisher 运行时，~1 Hz）
2. 点击 "Run All 32 Scenarios" → 控制台进度 1/32 → 32/32；total wall-clock < 90s
3. "View Report" 打开 HTML，Rule 13/14/15/16 + Multi-Ship 全绿；未覆盖 Rule ⚠️ 可见
4. 任意 ✅ 单元格跳转至 JSON（含 dcpa / arrow 输出路径引用）

**Showcase Bundle**：
- `reports/coverage_report_20260615.html`（静态，可离线，CCS 技术审查）
- `reports/batch_summary.arrow`（D2.5 / D3.6 消费入口）
- `scenarios/imazu22/MANIFEST.sha256`（Imazu-22 认证资产冻结证据）

---

## §19 风险与应对

| ID | 风险 | 概率 | 影响 | 应对 |
|---|---|---|---|---|
| R1 | cerberus-cpp FetchContent 在 Ubuntu 22.04 拉取 / 编译失败 | Medium | T-B4 +4h | 备选：`apt install` 本地 cerberus-cpp；或仅用 Python cerberus（C++ 验证降为 optional DoD）|
| R2 | Imazu-22 `avoidance_time_s` 参数不合理导致 pass 率低 | Medium | T-C2..4 调参 +3h | T-G1 中对 fail 场景输出 TCPA 值；按 0.55×TCPA 公式微调；MS 场景放宽 `min_dcpa_with_action_m` 至 300m |
| R3 | pybind11 Humble 集成配置复杂 | Medium | T-A1 +4h | 缓冲期消化；降级方案：subprocess 调用 C++ 可执行文件（仅影响 T-D1，不影响其他 track）|
| R4 | Arrow nullable 字段在 multi-target 场景中 ts2/ts3 null 处理 | Low | T-D4 +2h | pyarrow null 处理标准；在 `_write_arrow_trajectories` 中统一用 `None` 替代缺失 target 坐标 |
| R5 | 10 个原有场景迁移后 `ScenarioSpec.from_file()` ENU round-trip 误差影响 pass_criteria | Low | T-C1 返工 | T-C1 AC 要求：每个迁移场景的 dcpa_no_action 与 v1.0 差异 < 5m |

---

## §20 Finding 关闭路径

| Finding ID | 关闭条件 |
|---|---|
| **G P0-G-1(b)** | 32 个 maritime-schema v2.0 YAML 存在且 cerberus 验证通过；HTML 报告每 cell 链接 JSON；Imazu-22 SHA256 冻结 |
| **G P1-G-4** | `disturbance_model` 字段在 schema + 32 个 YAML 中存在（含 wind/current/vis/wave）；元数据写入 JSON 输出 |
| **G P1-G-5** | SIL HMI 通过 sil_router + sil_ws 复用 M8 前端；无独立 Web 应用 |
| **P2-E1** | 32 个场景 `wall_clock_s < 60`；evidence：`batch_results.json` + `batch_summary.arrow` |

关闭文档写入：`docs/Design/Review/2026-05-07/finding-closure/D1.3b.md`

---

## §21 D1.3b.1 全闭判据（2026-06-15 EOD）

- [ ] `scenarios/colregs/` 10 个 YAML v2.0 存在，Python + C++ cerberus 双验证通过
- [ ] `scenarios/imazu22/` 22 个 YAML 存在，Python + C++ cerberus 双验证通过
- [ ] `scenarios/imazu22/MANIFEST.sha256` 存在；`check-imazu22-hash.sh` 在 CI 中通过
- [ ] `batch_summary.arrow`（32 行）+ `batch_trajectories.arrow`（~30 万行）可读
- [ ] `reports/coverage_report_20260615.html` 存在；Rule 13/14/15/16 + Multi-Ship 覆盖行全绿
- [ ] 所有 32 场景 `wall_clock_s < 60`；来自 `batch_results.json`
- [ ] `/ws/sil_debug` 实时推送（mock publisher 运行时）
- [ ] `POST /sil/scenario/list` 返回 32 个文件名
- [ ] 10 个回归测试文件**全部 PASS**（含 test_sil_hmi.py 23 个）
- [ ] Finding 关闭文档：G P0-G-1(b) + G P1-G-4 + G P1-G-5 + P2-E1

---

## §22 下游 D 接口

| 下游 D | 消费产物 | 所需扩展 |
|---|---|---|
| **D1.3.1** Simulator Qualification | 复用 `ScenarioSpec` + `simulate()` | 传入 ManeuverType 场景（ScenarioSpec 格式不变）|
| **D1.6** 场景库 schema | `fcb_scenario_v2.yaml` 升为 v3.0；32 个 YAML 回填 `requirements_traced` + `hazid_id` | D1.3b.1 YAML 无需改动，仅新增字段 |
| **D2.5** SIL Integration replay | `batch_trajectories.arrow`（scenario_id 为 join key）| D2.5 直接读 Arrow 文件；batch_runner 不变 |
| **D3.6** SIL 1000 Monte Carlo | `batch_summary.arrow`（summary table）| D3.6 在 batch_runner 上叠加参数扫描循环；写出多个 Arrow 文件 |
| **D2.x** M1/M6 实装 | `sil_mock_publisher` 替换为真实节点（topic 名不变）| launch file 单行修改；HMI 零改动 |
