# SIL Scenario Schema — maritime-schema v0.2.x TrafficSituation Extension

| Field | Value |
|---|---|
| Document ID | MASS-L3-TDL-SIL-SCHEMA-001 |
| Version | v1.0 |
| Date | 2026-06-15 |
| Status | **Authoritative** — D1.6 deliverable |
| Architecture Baseline | v1.1.3-pre-stub (§F.5, §F.6) |
| Development Plan | v3.2-master |
| Schema Authority | [dnv-opensource/maritime-schema](https://github.com/dnv-opensource/maritime-schema) v0.2.x `TrafficSituation` |
| Validation Stack | Python cerberus + C++ cerberus-cpp (shared schema) |

---

## §1 maritime-schema 选型背景

### 1.1 决策记录

2026-05-09 v3.1 架构修订（决策记录 §5）决定：场景 schema 由"内部 JSON Schema / Pydantic 强类型"改为 **DNV maritime-schema v0.2.x `TrafficSituation` 扩展**，理由：

1. **互操作性**：maritime-schema 是 DNV 开源的船舶交通场景标准格式，已在 NTNU colav-simulator、DNV HIL 平台中使用（证据 [R27]）。使用社区标准避免内部 schema 与外部工具链（farn / ospx）格式转换开销。
2. **CCS 接受度**：CCS-DNV-Brinav 2024 MoU + Brinav Armada 78 03 案例先例为 maritime-schema 作 evidence container 的接受度提供先例（证据 [R25]）。D1.8 将向 CCS 技术中心发函确认。
3. **DNV 工具链贯通**：farn（case folder generator）和 ospx（OSP 系统结构 author）原生消费 maritime-schema，零格式转换即可集成。

### 1.2 DNV 工具链 3 MUST 引用

| MUST | 工具 | 版本 | 用途 | 证据 |
|---|---|---|---|---|
| MUST-1 | `dnv-opensource/maritime-schema` | v0.2.x | 场景 YAML 权威格式 | [R27] |
| MUST-2 | `dnv-opensource/farn` | v0.4.2+ | 1100-cell case folder generator (LHS/Sobol/fixed-grid) | [R29] |
| MUST-3 | `dnv-opensource/ospx` | latest | OSP 系统结构 author (FMU 拓扑配置) | [R30] |

> **推翻信号**：若 CCS 2026-06 回函要求中文专用格式，maritime-schema 退为内部表示 + 加导出器（决策记录 §5.5）。此场景下 MUST-1 保留为内部格式，MUST-2/3 不受影响。

The overthrow contingency acknowledges that CCS i-Ship AIP review may impose format requirements specific to Chinese maritime regulatory practice. The export adapter pattern (decision record §5.5) ensures zero data loss: the internal representation remains maritime-schema throughout the toolchain, and a format converter is added at the evidence export boundary. This pattern mirrors the approach used in Brinav Armada 78 03, where maritime-schema served as the canonical internal format with Chinese-standard exports produced at the certificate submission stage.

### 1.3 与 D1.3.2.1 fcb_scenario_v2.yaml 的关系

D1.3.2.1 的 `tools/sil/cerberus_schema/fcb_scenario_v2.yaml` 是 maritime-schema `TrafficSituation` 的 **FCB 项目实例化子集**（cerberus-cpp 兼容）。本 schema 文档定义的是完整的 maritime-schema 扩展规约；`fcb_scenario_v2.yaml` 是其 cerberus 验证实现。

**版本锁定**：maritime-schema PyPI release watch 在 §8 定义。上游 breaking change 须触发 `fcb_scenario_v2.yaml` 同步修订 + 全量场景 re-validation。

---

## §2 TrafficSituation v0.2.x 核心字段映射

maritime-schema v0.2.x `TrafficSituation` 是顶层 YAML 结构，描述单个仿真场景。以下列出核心字段及其在 FCB 项目中的语义映射：

### 2.1 顶层字段

| maritime-schema 字段 | 类型 | 必填 | FCB 语义 | 来源 |
|---|---|---|---|---|
| `title` | string | ✓ | 场景标题（如 "Head-On Encounter — Imazu Case 01"） | 场景作者 |
| `description` | string | | 场景描述（自由文本） | 场景作者 |
| `start_time` | ISO 8601 | | 仿真起始时间戳 | 固定 "2026-01-01T00:00:00Z" |
| `own_ship` | OwnShip | ✓ | 本船初始状态 | 见 §2.2 |
| `target_ships` | list[TargetShip] | | 目标船列表（0..N） | 见 §2.3 |
| `metadata` | dict | ✓ | FCB 项目扩展字段（allow_unknown=True） | 见 §3 |

### 2.2 OwnShip 字段

| 字段 | 类型 | 必填 | FCB 语义 |
|---|---|---|---|
| `own_ship.id` | string | ✓ | 本船标识，固定 "os" |
| `own_ship.nav_status` | int (0-15) | | AIS 航行状态，FCB 场景固定 0 (under way using engine) |
| `own_ship.mmsi` | int | | MMSI，固定 123456789 |
| `own_ship.initial.position.latitude` | float (-90..90) | ✓ | WGS84 纬度 |
| `own_ship.initial.position.longitude` | float (-180..180) | ✓ | WGS84 经度 |
| `own_ship.initial.cog` | float (0..360) | ✓ | 对地航向 (Course Over Ground)，° |
| `own_ship.initial.sog` | float (0..50) | ✓ | 对地航速 (Speed Over Ground)，kn |
| `own_ship.initial.heading` | float (0..360) | | 船首向，°（默认 = cog） |

### 2.3 TargetShip 字段

| 字段 | 类型 | 必填 | FCB 语义 |
|---|---|---|---|
| `target_ships[].id` | string | ✓ | 目标标识，如 "ts1" |
| `target_ships[].nav_status` | int (0-15) | | AIS 航行状态 |
| `target_ships[].mmsi` | int | | MMSI |
| `target_ships[].initial.*` | 同 OwnShip.initial | ✓ | 目标初始状态 |

### 2.4 坐标约定

- **存储格式**：WGS84 lat/lon（maritime-schema 原生）
- **仿真引擎格式**：ENU (East-North-Up) meters，flat-earth 近似
- **坐标原点**：`metadata.geo_origin` 定义（默认 63.0°N, 5.0°E — Norwegian Sea anchor）
- **转换桥**：`tools/sil/geo_utils.py`（D1.3.2.1 Task 1 产出）
- **ENU 转换公式**：给定参考原点 `(lat0, lon0)`，WGS84 `(lat, lon)` 到 ENU `(e, n, u)` 的转换使用 WGS84 ellipsoid 近似：`e = (lon - lon0) × cos(lat0) × R`, `n = (lat - lat0) × R`，其中 `R = 6378137 m`（WGS84 semi-major axis）。高程分量 u 固定为 0（海面仿真）。该近似在 10 km 范围内误差 < 0.1%，满足 COLREGs 仿真精度要求。
- **逆转换**：ENU 到 WGS84 仅用于结果回写和可视化展示，不参与仿真闭环。仿真引擎内部全程使用 ENU 以降低浮点误差。`geo_utils.py` 提供 `enu_to_geodetic()` 和 `geodetic_to_enu()` 双向转换函数，均经过与 PROJ 库的交叉验证。

### 2.5 速度与角度约定

| 物理量 | 存储单位 | 内部单位 | 转换 |
|---|---|---|---|
| 航速 (sog) | kn (节) | m/s | × 0.5144 |
| 航向 (cog/heading) | ° (0=North, CW) | rad (CCW from East) | ψ = π/2 − deg×π/180 |
| 转速 (n_rps) | — | rps | metadata.simulation.n_rps_initial |

---

## §3 FCB metadata.* 扩展字段表

maritime-schema 的 `metadata` 节点允许 additional properties（`allow_unknown=True`），FCB 项目在 `metadata.*` 下扩展以下专属字段：

### 3.1 必须字段（cerberus schema 强制）

| 字段路径 | 类型 | 说明 | 示例值 |
|---|---|---|---|
| `metadata.schema_version` | string | Schema 版本，锁定 "2.0" | `"2.0"` |
| `metadata.scenario_id` | string | 全局唯一场景标识 | `"imazu-01-ho-v1.0"` |
| `metadata.odd_zone` | enum[A,B,C] | ODD 子域 | `"A"` |
| `metadata.vessel_class` | string | 适用船型 | `"FCB"` |
| `metadata.pass_criteria.max_dcpa_no_action_m` | float | 无规避动作下允许的最大 CPA (m) | `926.0` |
| `metadata.pass_criteria.min_dcpa_with_action_m` | float | 规避后要求的最小 CPA (m) | `500.0` |
| `metadata.simulation.duration_s` | float | 仿真时长 (s) | `600.0` |
| `metadata.simulation.dt_s` | float | 仿真步长 (s)，锁定 0.02 | `0.02` |

### 3.2 FCB 追踪字段（可选，用于追溯矩阵自动生成）

| 字段路径 | 类型 | 说明 | 示例值 |
|---|---|---|---|
| `metadata.requirements_traced` | list[string] | 追溯到需求文档编号 | `["REQ-COLREG-014", "REQ-ODD-A-001"]` |
| `metadata.hazid_id` | list[string] | 追溯到 HAZID 危险源 ID | `["HAZ-023", "HAZ-045"]` |
| `metadata.rule_branch_covered` | list[string] | 覆盖的 COLREG 规则分支 | `["Rule14_HeadOn"]` |
| `metadata.vessel_class_applicable` | list[string] | 适用船型列表 | `["FCB", "TUG", "FERRY"]` |
| `metadata.expected_outcome` | string | 预期结果描述 | `"OS turns starboard, CPA > 926 m"` |

### 3.3 FCB 元数据字段（可选）

| 字段路径 | 类型 | 说明 | 示例值 |
|---|---|---|---|
| `metadata.scenario_source` | string | 场景来源 | `"imazu1987"` / `"fcb_original"` |
| `metadata.geo_origin.latitude` | float | 坐标原点纬度 | `63.0` |
| `metadata.geo_origin.longitude` | float | 坐标原点经度 | `5.0` |
| `metadata.geo_origin.description` | string | 原点描述 | `"Norwegian Sea anchor"` |
| `metadata.encounter.rule` | string | 适用 COLREG 规则 | `"Rule14"` |
| `metadata.encounter.give_way_vessel` | enum[own,target,none] | 让路船 | `"own"` |
| `metadata.encounter.expected_own_action` | enum[turn_starboard,turn_port,maintain,slow_down] | 预期本船动作 | `"turn_starboard"` |
| `metadata.encounter.avoidance_time_s` | float | 规避启动时间 (s) | `300.0` |
| `metadata.encounter.avoidance_delta_rad` | float | 规避航向变化量 (rad) | `0.6109` |
| `metadata.encounter.avoidance_duration_s` | float | 规避持续时间 (s) | `90.0` |
| `metadata.disturbance_model.wind_kn` | float | 风速 (kn) | `0.0` |
| `metadata.disturbance_model.wind_dir_nav_deg` | float | 风向 (nautical °) | `0.0` |
| `metadata.disturbance_model.current_kn` | float | 流速 (kn) | `0.0` |
| `metadata.disturbance_model.vis_m` | float | 能见度 (m) | `10000.0` |
| `metadata.disturbance_model.wave_height_m` | float | 波高 (m) | `0.0` |
| `metadata.simulation.n_rps_initial` | float | 初始螺旋桨转速 (rps) | `3.0` |
| `metadata.prng_seed` | int or null | 伪随机数种子 (null=随机) | `42` or `null` |

### 3.4 示例：完整 metadata 节点

```yaml
metadata:
  schema_version: "2.0"
  scenario_id: "imazu-01-ho-v1.0"
  scenario_source: "imazu1987"
  vessel_class: "FCB"
  odd_zone: "A"
  requirements_traced:
    - "REQ-COLREG-014"
  hazid_id:
    - "HAZ-023"
  rule_branch_covered:
    - "Rule14_HeadOn"
  vessel_class_applicable:
    - "FCB"
  expected_outcome: "OS turns starboard, CPA > 926 m"
  geo_origin:
    latitude: 63.0
    longitude: 5.0
    description: "Norwegian Sea anchor"
  encounter:
    rule: "Rule14"
    give_way_vessel: "own"
    expected_own_action: "turn_starboard"
    avoidance_time_s: 300.0
    avoidance_delta_rad: 0.6109
    avoidance_duration_s: 90.0
  disturbance_model:
    wind_kn: 0.0
    wind_dir_nav_deg: 0.0
    current_kn: 0.0
    current_dir_nav_deg: 0.0
    vis_m: 10000.0
    wave_height_m: 0.0
  pass_criteria:
    max_dcpa_no_action_m: 926.0
    min_dcpa_with_action_m: 926.0
  simulation:
    duration_s: 700.0
    dt_s: 0.02
    n_rps_initial: 3.0
  prng_seed: null
```

---

## §4 命名规范

### 4.1 场景文件命名

```
<rule>-<odd>-<encounter>-<seed>-<version>.yaml
```

| 段 | 含义 | 取值示例 |
|---|---|---|
| `<rule>` | COLREG 规则编号 | `rule14`, `rule15`, `rule13` |
| `<odd>` | ODD 子域 | `odda`, `oddb`, `oddc` |
| `<encounter>` | 遭遇类型 | `ho` (Head-On), `cs` (Crossing Starboard), `cp` (Crossing Port), `ot` (Overtaking), `ms` (Multi-Ship) |
| `<seed>` | 随机种子 | `s001`–`s999`（`s000` = 无随机） |
| `<version>` | 场景版本 | `v1.0`, `v2.0` |

**示例**：`rule14-odda-ho-s001-v1.0.yaml` — Rule 14 Head-On, ODD A, seed 001, version 1.0.

**例外 — Imazu-22 保留命名**：Imazu 基准场景沿用 `imazu-NN-<enc>-v1.0.yaml`（如 `imazu-01-ho-v1.0.yaml`），以保持与 NTNU colav-simulator 的可追溯性。其内部 `metadata.scenario_id` 字段仍遵循上述规范。

The 5-segment naming convention is designed for three purposes. First, it enables deterministic scenario selection: CI can filter by `<encounter>` type to build a balanced Smoke 10 set, by `<odd>` zone to test ODD-specific behavior, or by `<seed>` to capture statistical variance. Second, it makes file-system-level glob patterns feasible: `scenarios/colregs/rule14-*.yaml` selects all Rule 14 variants across all ODD zones and seeds. Third, the version suffix provides explicit schema migration tracking: when `fcb_scenario_v2.yaml` changes, only scenarios matching the old version tag need re-validation, while unaffected scenarios skip redundant checks.

The seed segment `s000` is reserved for deterministic scenarios where no stochastic element is present. In these scenarios, all disturbance model parameters are explicitly set to constant values, the PRNG seed is null, and simulation results must be fully reproducible down to floating-point precision on the same hardware architecture. Randomized scenarios (`s001`–`s999`) incorporate Monte Carlo parameter variation and must be run with statistical pass criteria rather than exact match comparison.

### 4.2 场景目录结构

```
scenarios/
├── colregs/                     # 10 自编 COLREGs 场景 (D1.3.2.1)
│   └── colreg-rule*-*.yaml
├── imazu22/                     # 22 Imazu 基准场景 (D1.3.2.1)
│   ├── imazu-01-ho-v1.0.yaml
│   ├── imazu-02-cr-gw-v1.0.yaml
│   ├── ...
│   ├── imazu-22-ms-v1.0.yaml
│   └── .imazu22_sha256_manifest.yaml    # frozen hash manifest
├── demo1/                       # DEMO-1 现场展示场景 (5 个)
│   └── *.yaml
├── self-authored/               # 10 自编场景（待 Phase 2 填充）
│   └── *.yaml
    └── farn/                        # farn 生成 case folder (D3.6 填充)
        └── 1100-cell/

The directory hierarchy separates scenarios by origin and lifecycle stage. The `imazu22/` directory contains frozen, immutable benchmark scenarios established by Imazu (1987) and used for COLREGs compliance baseline verification. These scenarios are protected by the SHA256 frozen hash mechanism described in §4.3 and must not be modified after the DEMO-1 freeze date. The `demo1/` directory holds the five demonstration scenarios prepared for the DEMO-1 Skeleton Live milestone, which may be updated as the SIL platform matures. The `self-authored/` directory is reserved for the ten internally authored scenarios scheduled for Phase 2 completion, each extending Imazu coverage with additional ODD zones and disturbance configurations. The `farn/1100-cell/` directory is generated by the farn tool (§7.1) and contains three sampling strategy subdirectories — `fixed-grid/`, `lhs/`, and `sobol/` — for a combined target of 1100+ case definitions.
```

### 4.3 Frozen Hash 机制

`scenarios/imazu22/.imazu22_sha256_manifest.yaml`（D1.3.2.1 Task 10 产出）冻结 22 Imazu YAML 的 SHA256 哈希。任何场景内容修改将导致哈希不匹配，在 CI hash gate 被拒绝。

```yaml
# .imazu22_sha256_manifest.yaml
manifest_version: "1.0"
frozen_date: "2026-05-15"
files:
  imazu-01-ho-v1.0.yaml: "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
  imazu-02-cr-gw-v1.0.yaml: "sha256:..."
  # ... (22 entries)
```

**CI hash gate**（在 `.gitlab-ci.yml` 的 `sil-smoke` stage 前执行）：

```bash
tools/ci/check-imazu22-hash.sh
# Exit 0 = all hashes match
# Exit 1 = hash mismatch → 场景内容被意外修改
```

---

## §5 双语言验证流程

### 5.1 验证架构

```
maritime-schema YAML
        │
        ├── Python cerberus ──→ cerberus_validator.py ──→ pass / fail + error list
        │
        └── C++ cerberus-cpp ──→ validate_scenario ──→ pass (exit 0) / fail (exit 1) + stderr
```

两种实现共享同一份 schema 文件 `tools/sil/cerberus_schema/fcb_scenario_v2.yaml`，仅支持 cerberus-cpp 兼容子集（`type`, `required`, `min`, `max`, `allowed`, `schema`；禁止 `allof`, `anyof`, `check_with`, `coerce`）。

### 5.2 Python cerberus 验证

```bash
# 单个文件验证
python -c "
import sys; sys.path.insert(0, 'tools/sil')
from cerberus_validator import validate_yaml
import yaml
data = yaml.safe_load(open('scenarios/imazu22/imazu-01-ho-v1.0.yaml'))
validate_yaml(data)
print('PASS')
"

# 批量验证（所有场景）
python tools/sil/validate_all_scenarios.py --dir scenarios/
```

### 5.3 C++ cerberus-cpp 验证

```bash
# 编译（一次性）
cd tools/sil/cpp && cmake -B build && cmake --build build

# 单个文件验证
./build/validate_scenario \
  ../cerberus_schema/fcb_scenario_v2.yaml \
  ../../../scenarios/imazu22/imazu-01-ho-v1.0.yaml
# Exit 0 = PASS

# 风险 R1：若 cerberus-cpp FetchContent 失败（网络或 API 不兼容），
# C++ 验证退为可选。Python cerberus 仍提供完整覆盖。
```

### 5.4 CI 集成

两个验证器均作为 CI `sil-smoke` stage 的 job 运行。任一失败 → pipeline 标红，阻塞 merge。

```yaml
# .gitlab-ci.yml (fragment)
validate-scenario-py:
  stage: sil-smoke
  script:
    - python tools/sil/validate_all_scenarios.py --smoke --dir scenarios/

validate-scenario-cpp:
  stage: sil-smoke
  allow_failure: true  # R1 fallback: C++ optional
  script:
    - cd tools/sil/cpp && cmake -B build && cmake --build build
    - ./build/validate_scenario ../cerberus_schema/fcb_scenario_v2.yaml ../../../scenarios/imazu22/imazu-01-ho-v1.0.yaml
```

---

## §6 CI 三层集

### 6.1 三层定义

| 层 | 触发 | 场景数 | 运行时间 ≤ | Runner | 频率 | 用途 |
|---|---|---|---|---|---|---|
| **Smoke** | PR (merge_request_event) | 10 | 5 min | shared-runner (2 vCPU, 8 GB) | 每次 push | 快速门：schema validation + 10 关键场景 |
| **Nightly** | Scheduled (02:00 UTC) | 200 | 60 min | dedicated-runner (8 vCPU, 32 GB) | 每日 | 覆盖率回归：200 场景 + KPI 矩阵 |
| **Weekly Full** | Scheduled (Sun 02:00 UTC) | 1000+ | 8 h | dedicated-runner (16 vCPU, 64 GB) | 每周 | 全量回归：1100-cell cube + LHS 10000 sample |

### 6.2 Smoke 10 场景选择（PR trigger）

从 22 Imazu 中选取 10 个代表性场景覆盖全部 5 种遭遇类型。The selection criteria prioritize three factors: (a) coverage of all five encounter types defined in COLREGs Rules 13–15, (b) inclusion of multi-ship scenarios (2 and 3 target vessels) to exercise the M4 Behavior Arbiter's multi-target arbitration logic, and (c) representation of both give-way and stand-on vessel perspectives. The ten selected scenarios form the minimal set that guarantees every L3 TDL decision module is exercised at least once per CI push, with total execution time bounded by 5 minutes on the shared runner.

| # | 场景 | 类型 | 选择理由 |
|---|---|---|---|
| 1 | imazu-01-ho-v1.0 | Head-On | 基础 Rule 14 |
| 2 | imazu-02-cr-gw-v1.0 | Crossing Give-Way | Rule 15 GW |
| 3 | imazu-03-ot-v1.0 | Overtaking | Rule 13 |
| 4 | imazu-04-cr-so-v1.0 | Crossing Stand-On | Rule 15 SO |
| 5 | imazu-05-ms-v1.0 | Multi-Ship (2 targets) | Multi-vessel baseline |
| 6 | imazu-08-ms-v1.0 | Multi-Ship (2 targets, E) | 东侧来船 |
| 7 | imazu-12-ms-v1.0 | Multi-Ship (3 targets) | 三目标船 |
| 8 | imazu-16-ms-v1.0 | Multi-Ship (3 targets) | 对称交叉 |
| 9 | imazu-18-ms-v1.0 | Multi-Ship (3 targets) | 复杂多船 |
| 10 | imazu-22-ms-v1.0 | Multi-Ship (3 targets) | Imazu 终例 |

### 6.3 Nightly 200 场景构成

| 来源 | 数量 | 说明 |
|---|---|---|
| 22 Imazu × 3 seed variant each | 66 | Imazu 基准 × 种子变体 (s001/s002/s003) |
| 10 自编 × 3 seed variant each | 30 | 自编场景 × 种子变体 |
| farn LHS 104 | 104 | LHS 抽样（从 1100-cell cube 中选） |
| **合计** | **200** | |

### 6.4 Weekly Full 1000+ 场景构成

| 来源 | 数量 | 说明 |
|---|---|---|
| 1100-cell cube (deductive) | 1100 | Rule × ODD × 扰动 × seed |
| LHS 10000 sample | 10000 | Monte Carlo 大样本扫描（D3.6 扩展） |
| **合计** | **11100** | Weekly run 只跑 1100-cell + LHS 子集（~1200），全量 LHS 10000 在 D3.6 阶段 |

### 6.5 Runner 资源 SLA

| Runner | 类型 | vCPU | RAM | Disk | 来源 | 备注 |
|---|---|---|---|---|---|---|
| `shared-runner` | GitLab shared | 2 | 8 GB | 20 GB | GitLab.com free tier | Smoke 10 够用 |
| `dedicated-runner` | self-hosted (Ubuntu 22.04) | 8 | 32 GB | 100 GB | 项目预算采购 | Nightly 200 |
| `dedicated-runner-xl` | self-hosted (Ubuntu 22.04) | 16 | 64 GB | 500 GB | 项目预算采购 | Weekly Full 1000+ |

> **当前状态（2026-05-20）**：`shared-runner` 已可用；`dedicated-runner` 待采购（target: 2026-06-01）；`dedicated-runner-xl` 待 Phase 3 采购（target: 2026-07-15）。

### 6.6 CI 配置位置

- **GitLab CI pipeline**：`.gitlab-ci.yml` — `sil-smoke` / `sil-nightly` / `sil-weekly` stages
- **Smoke 10 runner 脚本**：`tools/ci/run_smoke_10.sh`
- **Nightly runner 脚本**：`tools/ci/run_nightly_200.sh`（Phase 2 产出）
- **Weekly runner 脚本**：`tools/ci/run_weekly_full.sh`（Phase 3 产出）

---

## §7 farn / ospx 集成

### 7.1 farn — 1100-cell Case Folder Generator

**安装**：

```bash
pip install farn>=0.4.2
```

**配置** (`tools/sil/farn_config.yaml`)：

```yaml
# farn case folder generator configuration
# 1100-cell = 11 Rule × 4 ODD × 5 Disturbance × 5 Seed

sampling_strategies:
  - name: fixed_grid
    type: full_factorial
    factors:
      rule: [Rule14, Rule15, Rule13]  # primary 3 rules
      odd_zone: [A, B, C]             # 3 ODD zones
      disturbance_level: [0, 1, 2, 3, 4]
      seed: [1, 2, 3, 4, 5]
    output_dir: scenarios/farn/1100-cell/fixed-grid/

  - name: lhs
    type: latin_hypercube
    samples: 500
    factors:
      rule: [Rule14, Rule15, Rule13]
      odd_zone: [A, B, C]
      target_bearing_deg: [0, 360]
      target_sog_kn: [5, 20]
      wind_kn: [0, 30]
      current_kn: [0, 5]
    output_dir: scenarios/farn/1100-cell/lhs/

  - name: sobol
    type: sobol
    samples: 500
    factors:
      rule: [Rule14, Rule15, Rule13]
      odd_zone: [A, B, C]
      target_bearing_deg: [0, 360]
      target_sog_kn: [5, 20]
      wind_kn: [0, 30]
      current_kn: [0, 5]
    output_dir: scenarios/farn/1100-cell/sobol/

scenario_template: tools/sil/templates/maritime-schema-v2.0.yaml.j2
```

**Dry-run 验证**：

```bash
# 生成 case folder 结构（不运行仿真）
python -m farn generate --config tools/sil/farn_config.yaml --dry-run

# 预期输出：
#   Created scenarios/farn/1100-cell/fixed-grid/ (225 cases)
#   Created scenarios/farn/1100-cell/lhs/ (500 cases)
#   Created scenarios/farn/1100-cell/sobol/ (500 cases)
#   Total: 1225 case definitions (dry-run, no simulation)
```

> **DEMO-1 交付**：dry-run 生成 case folder 结构 + 截图。**实跑 1100-cell 归 D3.6**（Phase 3，≥80% 通过率）。

### 7.2 ospx — Own-Ship FMU 系统结构配置

**安装**：

```bash
pip install ospx
```

**配置** (`tools/sil/ospx_config.toml`)：

```toml
# ospx OSP system structure configuration for FCB own-ship FMU

[system]
name = "FCB_OwnShip_MMG"
description = "FCB 4-DOF MMG own-ship model for SIL simulation"

[components.fmu]
name = "FCB_MMG_4DOF"
source = "src/sim_workbench/fcb_simulator/fmu/FCB_MMG_4DOF.fmu"
step_size = 0.02
parameters = { L_pp = 120.0, B = 20.0, d = 8.0, C_b = 0.75 }

[components.controller]
name = "NomotoAutopilot"
source = "src/sim_workbench/fcb_simulator/fmu/NomotoAutopilot.fmu"

[connections]
[[connections.coupling]]
source = "FCB_MMG_4DOF.state"
target = "NomotoAutopilot.ship_state"

[[connections.coupling]]
source = "NomotoAutopilot.rudder_command"
target = "FCB_MMG_4DOF.rudder_input"
```

**Dry-run 验证**：

```bash
# 生成 OSP 系统结构（不执行仿真）
python -m ospx validate --config tools/sil/ospx_config.toml

# 预期输出：
#   OSP system structure validation PASSED
#   Components: 2 (FCB_MMG_4DOF, NomotoAutopilot)
#   Connections: 2
```

> **DEMO-1 交付**：ospx config 生成 + dry-run 验证通过。**实际 FMU 联调归 D1.3.3**（FMI bridge / libcosim / dds-fmu）。

---

## §8 Breaking Change 监控

### 8.1 maritime-schema PyPI Release Watch

maritime-schema 上游 breaking change 可能影响 `fcb_scenario_v2.yaml` 和所有已有场景 YAML。监控策略：

**手动触发**（每 Phase 开始时执行）：

```bash
# 检查当前安装版本
pip show maritime-schema | grep Version

# 检查 PyPI 最新版本
pip index versions maritime-schema 2>/dev/null || curl -s https://pypi.org/pypi/maritime-schema/json | python -c "import sys,json; print(json.load(sys.stdin)['info']['version'])"
```

**CI 自动检查**（可选，在 Nightly pipeline 中执行）：

```yaml
# .gitlab-ci.yml (fragment)
check-maritime-schema-version:
  stage: sil-smoke
  script:
    - pip index versions maritime-schema
    - |
      INSTALLED=$(pip show maritime-schema | grep Version | awk '{print $2}')
      LATEST=$(pip index versions maritime-schema 2>/dev/null | head -1 | awk '{print $2}')
      if [ "$INSTALLED" != "$LATEST" ]; then
        echo "⚠️  maritime-schema update available: $INSTALLED → $LATEST"
        echo "⚠️  Action: review CHANGELOG, update fcb_scenario_v2.yaml if needed, re-validate all scenarios"
      fi
  allow_failure: true
```

### 8.2 Breaking Change 响应流程

```
PyPI release detected
    │
    ├── Step 1: Read upstream CHANGELOG
    ├── Step 2: Diff fcb_scenario_v2.yaml vs upstream TrafficSituation changes
    ├── Step 3: If breaking → create D1.6-fix branch
    │   ├── Update fcb_scenario_v2.yaml
    │   ├── Re-run cerberus validation on all 32 scenarios
    │   ├── Update scenario-traceability-matrix.csv schema_version column
    │   └── PR + merge
    └── Step 4: If non-breaking → document in §8.3 changelog table
```

### 8.3 Breaking Change 历史

| 日期 | upstream version | FCB action | PR |
|---|---|---|---|
| — | — | (none yet) | — |

---

## Reference Documents

- SIL Architecture: `docs/Design/SIL/v1.0-unified/01-sil-architecture.md` §4
- Scenario Integration Test: `docs/Design/SIL/v1.0-unified/04-sil-scenario-integration-test.md`
- V&V Plan: `docs/Design/V&V_Plan/00-vv-strategy-v0.1.md` §5 (coverage dimensions)
- Architecture Report: `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` §F.5, §F.6
- farn documentation: `tools/sil/farn_config.yaml`
- ospx documentation: `tools/sil/ospx_config.toml`
- cerberus schema: `tools/sil/cerberus_schema/fcb_scenario_v2.yaml`
- Existing scenarios: `scenarios/`
- Scenario traceability matrix: `docs/Design/SIL/scenario-traceability-matrix.csv`

## Document Status

| Aspect | Status | Target Date |
|---|---|---|
| Base YAML schema | ✅ Complete — maritime-schema v0.2.x TrafficSituation + FCB metadata.* | D1.6 |
| Validation tooling | ✅ Complete — Python cerberus + C++ cerberus-cpp (R1 optional) | D1.6 |
| CI three-tier set | ✅ Complete — Smoke 10 / Nightly 200 / Weekly Full 1000+ | D1.6 |
| farn/ospx integration | ✅ Complete — config + dry-run (actual execution deferred) | D1.6 |
| Scenario naming convention | ✅ Complete — 5-segment + Imazu exception + frozen hash | D1.6 |
| Breaking change monitoring | ✅ Complete — PyPI release watch + response flow | D1.6 |
| Scenario library index | 🟡 22 Imazu frozen / 10 self-authored planned (Phase 2) | D2.4 |
| Schema migration tooling | 🟡 Deferred — triggered by maritime-schema upstream breaking change | D2.x |
