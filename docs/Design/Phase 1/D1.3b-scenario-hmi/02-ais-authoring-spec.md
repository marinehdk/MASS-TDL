# D1.3b.2 · AIS-driven Scenario Authoring 工具 · 执行 Spec

**版本**：v1.0
**日期**：2026-05-11
**v3.1 D 编号**：D1.3b.2（D1.3b 第二工作单元）
**Owner**：技术负责人 + V&V 工程师
**完成日期**：2026-07-15 EOD（DEMO-2）
**工时预算**：4.0 人周（160h；5/25–7/15）
**关闭 finding**：G P0-G-1(c)（v3.1 NEW）

---

## §0 设计决策记录（brainstorming 锁定，D1.3b.2 内不再议）

| 决策 | 内容 | Rationale |
|---|---|---|
| **包结构** | 新建独立 colcon 包 `src/sim_workbench/scenario_authoring/`，子模块按管线阶段拆：`pipeline/` + `authoring/` + `replay/`；复用 ais_bridge 的 `nmea_decoder.AISRecord` 但不改 ais_bridge 一行代码 | colav-simulator 中 scenario_generator 是顶层独立模块；管线与解码器职责不同（解码→ROS2 topic vs 原始数据→YAML）；独立包可独立测试、独立 CI lint；通过 `from ais_bridge.nmea_decoder import AISRecord` 复用 |
| **数据源接入** | 下载脚本 + 本地 CSV 缓存（colav-simulator 模式）；`scripts/download_kystverket.py` / `scripts/download_noaa.py` 一次性下载，数据存 `data/ais_datasets/kystverket/` / `data/ais_datasets/noaa/`；管线从本地 CSV 读 | colav-simulator 预下载 CSV 到 `data/`，离线批处理；1h Kystverket 数据集 ~50-100MB，pandas 可直接处理；CCS 审计可通过 data hash 验证 |
| **COG 360° wrap 插值** | `np.unwrap(cog_rad)` → `np.interp` 线性插值 → `% 360.0` 回绕 | colav-simulator `math_functions.unwrap_angle` 同算法；`np.unwrap` 是 numpy 内置，零额外依赖；线性插值对 AIS 2Hz 上采样精度足够 |
| **50Hz 插值（2Hz→50Hz）** | 纯 numpy 线性插值（`np.interp`），位置/SOG/COG 各自独立；COG 用 unwrap+线性+回绕；Kalman 平滑留 D2.4 `ncdm_vessel` 实现 | AIS 2Hz→50Hz 是 25× 上采样，线性插值误差 < 1m（12kn 时 0.5s 航行 3m，线性分割 25 段每段 0.12m），对 DCPA 的影响远小于 AIS 原始定位误差（σ=50m） |
| **maritime-schema 字段对齐** | D1.3b.2 产出 YAML 使用 D1.3b.1 的 `fcb_scenario_v2.yaml` schema，新增 AIS 专属字段（`ais_source_hash` / `ais_mmsi[]` / `ais_time_window`）写入 `metadata.*`；cerberus `allow_unknown=True` 天然兼容 | D1.3b.1 `ScenarioSpec.from_file()` 读 metadata 是 dict 访问不校验未知 key；D1.3b.2 直接写 `metadata.*` 即可被 batch_runner 消费，不做 adapter 转换 |
| **DCPA/TCPA 计算** | 相对运动法（velocity obstacle 前置步骤），纯 numpy ~15 行实现，在 `stage4_filter.py` 中独立实现 | 离线管线是纯 Python 批处理工具，不启动 ROS2 graph；公式确定（IMO 标准），不与 M2 World Model 耦合；管线只需 ≤500m 粗过滤 |
| **L1 mode switching** | 配置驱动（colav-simulator 模式）：读场景 YAML 的 `metadata.scenario_source` → `"ais_derived"` 启动 ais_replay_node / 否则启动 sil_mock_node；不做显式 `l1_mode` launch arg | colav-simulator 用 `config.ais_data_file` 的 null/非 null 隐式决定 mode；配置驱动比 launch arg 更灵活，避免外部参数与场景内容不一致 |
| **坐标变换** | pyproj Transverse Mercator（自适应 UTM zone），colav-simulator 做法；复用 `pyproj`（ROS2 Humble 容器标准依赖） | flat-earth 仅适用 <50km 单区域；NOAA SF Bay（zone 10）和 Kystverket Trondheim（zone 33）跨 zone，pyproj 自适应 |

---

## §1 现有基础设施审计

### 1.1 可复用工件（D1.3a ais_bridge）

| 工件 | 路径 | 能力 | D1.3b.2 复用方式 |
|---|---|---|---|
| `nmea_decoder.py` | `src/sim_workbench/ais_bridge/ais_bridge/` | pyais AIVDM/AIVDO → AISRecord dataclass (mmsi/lat/lon/sog/cog/hdg/ship_type) | `from ais_bridge.nmea_decoder import AISRecord, decode_file` — D1.3b.2 的 Kystverket NMEA 数据复用 pyais 解析 |
| `dataset_loader.py` | 同上 | NOAA CSV / DMA NMEA 两种格式的 loader | `load_noaa_csv()` 复用 NOAA 格式解析；Kystverket 数据格式待下载后确认 |
| `target_publisher.py` | 同上 | AISRecord → TrackedTargetArray ROS2 msg | 复用 `build_tracked_target_array()` 字段映射逻辑（不直接调，参考实现） |
| `AISRecord` dataclass | `nmea_decoder.py:12-21` | mmsi/lat/lon/sog_kn/cog_deg/heading_deg/ship_type | D1.3b.2 管线输入统一使用 AISRecord，保持与 ais_bridge 同构 |
| AIS 合成数据集 | `data/ais_datasets/AIS_synthetic_1h.csv` | 合成数据，72,001 行，7.9 MB | 作为管线开发的测试输入；DoD 要求替换为真实 Kystverket 数据 |

### 1.2 缺口（不存在，须新建）

| 工件 | 说明 |
|---|---|
| 5 阶段预处理管线 | MMSI 分组 → 间隙拆段 → 重采样/插值/平滑 → DCPA 过滤 → COLREG 分类 |
| maritime-schema YAML 导出 | encounter 片段 → `scenarios/ais_derived/*.yaml` + Cerberus 验证 |
| `ais_replay_node.py` | ROS2 50Hz 回放节点，消费 encounter trajectory，发布 `/world_model/tracks` |
| `ncdm_vessel` / `intelligent_vessel` stub | 接口定义（实现延 D2.4 / D3.6） |
| 真实 AIS 数据集下载 | Kystverket / NOAA MarineCadastre 开放数据，≥1h 片段 |
| pyproj 坐标变换 | WGS84 → UTM NE Cartesian，自适应 zone |

### 1.3 数据集格式审计

**已确认**：
- `data/ais_datasets/AIS_synthetic_1h.csv`：合成 CSV，供管线开发测试
- 真实 AIS 数据须在 T1（数据集下载）完成后替换

**待下载后确认**（T1 AC 项）：
- Kystverket 数据格式（NMEA 0183 文件或 CSV）
- NOAA MarineCadastre 数据格式（CSV，字段与 D1.3a dataset_loader 的 `load_noaa_csv()` 预期一致）
- 实际覆盖时段 + MMSI 数量
- 文件大小（评估内存管线可行性 vs DuckDB 需求）

### 1.4 pyproj 环境确认

pyproj 是 ROS2 Humble 容器的间接依赖（geographic_msgs → ... → pyproj），D1.3b.2 不需要额外安装。如容器内缺失，在 `package.xml` 加 `<exec_depend>python3-pyproj</exec_depend>`。

---

## §2 包结构

```
src/sim_workbench/scenario_authoring/               ← NEW colcon 包
├── scenario_authoring/
│   ├── __init__.py
│   ├── pipeline/                                    # 5 阶段管线
│   │   ├── __init__.py
│   │   ├── stage1_group.py                          # MMSI 分组 + 去重 + 排序
│   │   ├── stage2_resample.py                       # 间隙拆段（>5 min）+ Δt=0.5s 重采样
│   │   ├── stage3_smooth.py                         # NE 线性插值 + COG 圆形插值 + Savitzky-Golay
│   │   ├── stage4_filter.py                         # DCPA/TCPA 过滤 + COLREG 几何分类
│   │   └── stage5_export.py                         # maritime-schema YAML 导出
│   ├── authoring/                                   # 管线编排 + 数据源适配
│   │   ├── __init__.py
│   │   ├── encounter_extractor.py                   # 管线编排器（调 stage1→5 全流程）
│   │   └── ais_source.py                            # Kystverket/NOAA 数据源适配器
│   ├── replay/                                      # ROS2 回放节点 + target 模式
│   │   ├── __init__.py
│   │   ├── ais_replay_node.py                       # 50Hz 历史轨迹回放
│   │   └── target_modes.py                          # ncdm_vessel / intelligent_vessel stub
│   └── config/
│       └── scenario_authoring.yaml                  # 默认配置（DCPA 阈值 / replay_rate 等）
├── scripts/
│   ├── download_kystverket.py                       # Kystverket AIS 数据下载
│   └── download_noaa.py                             # NOAA MarineCadastre AIS 数据下载
├── launch/
│   └── scenario_authoring.launch.py                 # 统一入口（配置驱动 mode）
├── test/
│   ├── test_pipeline_stage1.py
│   ├── test_pipeline_stage2.py
│   ├── test_pipeline_stage3.py
│   ├── test_pipeline_stage4.py
│   ├── test_pipeline_stage5.py
│   ├── test_encounter_extractor.py
│   ├── test_ais_replay_node.py
│   └── test_target_modes.py
├── setup.py
└── package.xml
```

**不改动文件**（回归边界）：
- `src/sim_workbench/ais_bridge/**` — 零修改
- `src/sim_workbench/sil_mock_publisher/**` — 零修改
- `src/sim_workbench/fcb_simulator/**` — 零修改

---

## §3 5 阶段管线详细设计

### 3.1 数据流总览

```
原始AIS CSV/NMEA
     │
     ▼
┌─────────────────────────────────────────────────────────────────┐
│ Stage 1: MMSI 分组 + 去重 + 时间排序                              │
│   输入: list[AISRecord] (所有船混杂)                              │
│   输出: dict[MMSI, list[AISRecord]] (每船独立时间序列)             │
│   丢弃: MMSI 出现次数 < 10 的单船（不足以构成轨迹）                 │
└──────────────┬──────────────────────────────────────────────────┘
               │ per-MMSI
               ▼
┌─────────────────────────────────────────────────────────────────┐
│ Stage 2: 间隙拆段 + 重采样                                        │
│   输入: list[AISRecord] (单船时间序列)                             │
│   输出: list[TrajectorySegment] (每个连续段)                       │
│   规则: 相邻报告间隔 > 5 min → 拆段；段长 < 2 min → 丢弃           │
│   重采样: 对每段 Δt=0.5s 线性插值（位置）+ COG unwrap              │
└──────────────┬──────────────────────────────────────────────────┘
               │ per-segment
               ▼
┌─────────────────────────────────────────────────────────────────┐
│ Stage 3: 平滑（Savitzky-Golay）                                    │
│   输入: TrajectorySegment (0.5s 重采样后)                          │
│   输出: TrajectorySegment (平滑后)                                 │
│   SOG: Savitzky-Golay filter (window=21, order=3)                │
│   COG: unwrap → Savitzky-Golay → re-wrap                         │
│   位置: 不对位置做平滑（重采样插值已足够）                           │
└──────────────┬──────────────────────────────────────────────────┘
               │ all segments (all MMSI)
               ▼
┌─────────────────────────────────────────────────────────────────┐
│ Stage 4: DCPA/TCPA 过滤 + COLREG 几何分类                         │
│   对每对 (own-ship 轨迹, target-ship 轨迹) 在时间对齐窗口内:        │
│     a) 计算 min DCPA, min TCPA（相对运动法）                       │
│     b) 过滤: min DCPA < 500m AND min TCPA < 20 min              │
│     c) 对通过过滤的 encounter 做 COLREG 几何分类:                  │
│        - bearing 在 [112.5°, 247.5°] → OVERTAKING               │
│        - bearing in [0°, 6°] ∪ [354°, 360°] AND heading_diff     │
│          > 170° → HEAD_ON                                       │
│        - 其余 → CROSSING                                         │
│   输出: list[Encounter]                                          │
└──────────────┬──────────────────────────────────────────────────┘
               │ per-encounter
               ▼
┌─────────────────────────────────────────────────────────────────┐
│ Stage 5: maritime-schema YAML 导出                               │
│   输入: Encounter (含 own-ship + target 轨迹片段)                  │
│   输出: scenarios/ais_derived/ais_{MMSI}_{timestamp}_{type}.yaml │
│   含: TrafficSituation + metadata.* 扩展 + Cerberus 验证          │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 Stage 1 — MMSI 分组 + 去重

```python
# scenario_authoring/pipeline/stage1_group.py

def group_by_mmsi(records: list[AISRecord]) -> dict[int, list[AISRecord]]:
    """Group AIS records by MMSI, filter sparse vessels (<10 records),
    sort each vessel's records by timestamp.
    """
    from collections import defaultdict

    groups: dict[int, list[AISRecord]] = defaultdict(list)
    for rec in records:
        groups[rec.mmsi].append(rec)

    # Filter: keep only vessels with >= 10 position reports
    result = {}
    for mmsi, recs in groups.items():
        if len(recs) >= 10:
            recs.sort(key=lambda r: r.timestamp)  # AISRecord needs timestamp field
            result[mmsi] = recs

    return result
```

**AISRecord 扩展**：`AISRecord` dataclass（来自 ais_bridge）当前无 `timestamp` 字段。D1.3b.2 在管线内部定义扩展版本：

```python
@dataclass
class TimedAISRecord(AISRecord):
    """AISRecord with parsed timestamp for pipeline processing."""
    timestamp: float  # Unix epoch seconds
```

`from_ais_record()` 工厂方法从 pyais 的 `decoded.rx_time` 或 NOAA CSV 的 `BaseDateTime` 列提取时间戳。

### 3.3 Stage 2 — 间隙拆段 + 重采样

```python
# scenario_authoring/pipeline/stage2_resample.py

@dataclass
class TrajectorySegment:
    mmsi: int
    t_start: float
    t_end: float
    t: np.ndarray          # time axis (s), Δt=0.5s
    lat: np.ndarray        # WGS84 lat
    lon: np.ndarray        # WGS84 lon
    sog_kn: np.ndarray
    cog_deg: np.ndarray
    heading_deg: np.ndarray

GAP_THRESHOLD_S = 300.0    # 5 minutes
MIN_SEGMENT_DURATION_S = 120.0  # discard segments < 2 min
RESAMPLE_DT = 0.5          # seconds


def split_into_segments(
    records: list[TimedAISRecord],
    gap_threshold_s: float = GAP_THRESHOLD_S,
    min_duration_s: float = MIN_SEGMENT_DURATION_S,
) -> list[TrajectorySegment]:
    """Split vessel track into continuous segments at temporal gaps > 5 min.
    Discard segments shorter than 2 min.
    """
    segments = []
    current: list[TimedAISRecord] = [records[0]]

    for i in range(1, len(records)):
        gap = records[i].timestamp - records[i-1].timestamp
        if gap > gap_threshold_s:
            if _duration(current) >= min_duration_s:
                segments.append(_resample_segment(current))
            current = [records[i]]
        else:
            current.append(records[i])

    if _duration(current) >= min_duration_s:
        segments.append(_resample_segment(current))

    return segments


def _resample_segment(records: list[TimedAISRecord]) -> TrajectorySegment:
    """Resample segment to uniform Δt=0.5s grid with linear interpolation."""
    t_orig = np.array([r.timestamp for r in records])
    t_new = np.arange(t_orig[0], t_orig[-1], RESAMPLE_DT)

    lat_new = np.interp(t_new, t_orig, [r.lat for r in records])
    lon_new = np.interp(t_new, t_orig, [r.lon for r in records])
    sog_new = np.interp(t_new, t_orig, [r.sog_kn for r in records])

    # COG: unwrap → interp → re-wrap (per §0 decision)
    cog_rad = np.radians([r.cog_deg for r in records])
    cog_unwrapped = np.unwrap(cog_rad)
    cog_interp_rad = np.interp(t_new, t_orig, cog_unwrapped)
    cog_new = np.degrees(cog_interp_rad) % 360.0

    # Heading: same treatment as COG
    hdg_rad = np.radians([r.heading_deg for r in records])
    hdg_unwrapped = np.unwrap(hdg_rad)
    hdg_interp_rad = np.interp(t_new, t_orig, hdg_unwrapped)
    hdg_new = np.degrees(hdg_interp_rad) % 360.0

    return TrajectorySegment(
        mmsi=records[0].mmsi,
        t_start=t_new[0], t_end=t_new[-1],
        t=t_new, lat=lat_new, lon=lon_new,
        sog_kn=sog_new, cog_deg=cog_new, heading_deg=hdg_new,
    )
```

### 3.4 Stage 3 — Savitzky-Golay 平滑

```python
# scenario_authoring/pipeline/stage3_smooth.py
from scipy.signal import savgol_filter

SG_WINDOW = 21   # ~10.5s at 0.5s dt
SG_ORDER = 3


def smooth_segment(seg: TrajectorySegment) -> TrajectorySegment:
    """Apply Savitzky-Golay filter to SOG and COG.
    Position is NOT smoothed (interpolation from Stage 2 is sufficient).
    """
    sog_smoothed = savgol_filter(seg.sog_kn, SG_WINDOW, SG_ORDER)

    cog_rad = np.radians(seg.cog_deg)
    cog_unwrapped = np.unwrap(cog_rad)
    cog_smoothed_rad = savgol_filter(cog_unwrapped, SG_WINDOW, SG_ORDER)
    cog_smoothed = np.degrees(cog_smoothed_rad) % 360.0

    return TrajectorySegment(
        mmsi=seg.mmsi,
        t_start=seg.t_start, t_end=seg.t_end,
        t=seg.t, lat=seg.lat, lon=seg.lon,       # position: pass-through
        sog_kn=sog_smoothed, cog_deg=cog_smoothed, heading_deg=seg.heading_deg,
    )
```

**平滑选择依据**：Savitzky-Golay 对局部多项式拟合，保留突变特征（如真实转向），不像移动平均会模糊转向事件。Kalman 平滑精度更高但需调参（过程噪声 Q / 观测噪声 R），且 colav-simulator 论文的 Kalman 管线约 300 行。D1.3b.2 先通链路，Kalman 留 D2.4。

### 3.5 Stage 4 — DCPA/TCPA 过滤 + COLREG 几何分类

```python
# scenario_authoring/pipeline/stage4_filter.py
from dataclasses import dataclass
import numpy as np

DCPA_THRESHOLD_M = 500.0     # DoD: DCPA < 500m
TCPA_THRESHOLD_S = 1200.0    # 20 minutes


@dataclass
class Encounter:
    """A single COLREGs encounter extracted from AIS data."""
    own_mmsi: int
    target_mmsi: int
    encounter_type: str       # "HEAD_ON" | "CROSSING" | "OVERTAKING"
    min_dcpa_m: float
    min_tcpa_s: float
    t_encounter_start: float
    t_encounter_end: float
    own_trajectory: np.ndarray     # (N, 5): t, lat, lon, sog, cog
    target_trajectory: np.ndarray  # (N, 5): t, lat, lon, sog, cog


def compute_dcpa_tcpa_relative_motion(
    os_lat: np.ndarray, os_lon: np.ndarray, os_sog: np.ndarray, os_cog: np.ndarray,
    ts_lat: np.ndarray, ts_lon: np.ndarray, ts_sog: np.ndarray, ts_cog: np.ndarray,
) -> tuple[float, float]:
    """Compute min DCPA and corresponding TCPA using relative motion method.

    DCPA = |p_r - (p_r·v_r)/|v_r|² * v_r|
    where p_r = position of target relative to own-ship,
          v_r = velocity of target relative to own-ship.

    Returns (min_dcpa_m, tcpa_at_min_dcpa_s).
    """
    # Convert to NE Cartesian (flat-earth approximation at encounter scale ~10km)
    from scenario_authoring.authoring.ais_source import latlon_to_ne

    os_n, os_e = latlon_to_ne(os_lat, os_lon)
    ts_n, ts_e = latlon_to_ne(ts_lat, ts_lon)

    # Velocity components (NE)
    os_vn = os_sog * np.cos(np.radians(os_cog))
    os_ve = os_sog * np.sin(np.radians(os_cog))
    ts_vn = ts_sog * np.cos(np.radians(ts_cog))
    ts_ve = ts_sog * np.sin(np.radians(ts_cog))

    # Relative position and velocity
    p_n = ts_n - os_n
    p_e = ts_e - os_e
    v_n = ts_vn - os_vn
    v_e = ts_ve - os_ve

    # For each timestep: TCPA = -(p·v) / |v|²
    dot_pv = p_n * v_n + p_e * v_e
    v_sq = v_n**2 + v_e**2

    # Avoid division by zero (parallel or one ship stationary)
    tcpa = np.full_like(dot_pv, np.inf)
    mask = v_sq > 1e-6
    tcpa[mask] = -dot_pv[mask] / v_sq[mask]

    # DCPA at TCPA for each timestep
    dcpa_n = p_n + tcpa * v_n
    dcpa_e = p_e + tcpa * v_e
    dcpa = np.sqrt(dcpa_n**2 + dcpa_e**2)

    # Filter to valid TCPA range (positive, within time window)
    valid = (tcpa > 0) & (tcpa < TCPA_THRESHOLD_S)
    if not valid.any():
        return np.inf, np.inf

    min_idx = np.argmin(dcpa[valid])
    return dcpa[valid][min_idx], tcpa[valid][min_idx]


def classify_colreg_encounter(
    bearing_deg: float, target_heading_deg: float, own_heading_deg: float,
) -> str:
    """COLREG geometric pre-classification (same logic as M2 World Model §6.3.1).

    bearing_deg: bearing of target from own-ship (degrees, 0°=North).
    target_heading_deg: target ship heading.
    own_heading_deg: own-ship heading.
    """
    # OVERTAKING: target bearing in own-ship stern sector [112.5°, 247.5°]
    if 112.5 <= bearing_deg <= 247.5:
        return "OVERTAKING"

    # HEAD-ON: target bearing near bow + opposing courses
    heading_diff = abs(target_heading_deg - own_heading_deg) % 360.0
    if heading_diff > 180.0:
        heading_diff = 360.0 - heading_diff
    if (bearing_deg < 6.0 or bearing_deg > 354.0) and heading_diff > 170.0:
        return "HEAD_ON"

    return "CROSSING"


def filter_encounters(
    own_segments: list[TrajectorySegment],
    target_segments: list[TrajectorySegment],
    dcpa_threshold_m: float = DCPA_THRESHOLD_M,
) -> list[Encounter]:
    """Find all COLREGs encounters between own-ship and target-ship segments.

    For each (own, target) segment pair with temporal overlap:
      1. Compute min DCPA/TCPA via relative motion
      2. Filter by DCPA threshold
      3. Classify COLREG type
    Returns list of Encounter objects for qualifying encounters.
    """
    encounters = []
    for os_seg in own_segments:
        for ts_seg in target_segments:
            # Temporal overlap check
            t_start = max(os_seg.t_start, ts_seg.t_start)
            t_end = min(os_seg.t_end, ts_seg.t_end)
            if t_end - t_start < 60.0:  # minimum 60s overlap
                continue

            # Align trajectories in overlapping time window
            os_mask = (os_seg.t >= t_start) & (os_seg.t <= t_end)
            ts_mask = (ts_seg.t >= t_start) & (ts_seg.t <= t_end)

            if os_mask.sum() < 10 or ts_mask.sum() < 10:
                continue

            min_dcpa, tcpa_at_min = compute_dcpa_tcpa_relative_motion(
                os_seg.lat[os_mask], os_seg.lon[os_mask],
                os_seg.sog_kn[os_mask], os_seg.cog_deg[os_mask],
                ts_seg.lat[ts_mask], ts_seg.lon[ts_mask],
                ts_seg.sog_kn[ts_mask], ts_seg.cog_deg[ts_mask],
            )

            if min_dcpa < dcpa_threshold_m and tcpa_at_min < TCPA_THRESHOLD_S:
                # Compute average bearing of target from own-ship
                mean_bearing = _compute_mean_bearing(
                    os_seg.lat[os_mask], os_seg.lon[os_mask],
                    ts_seg.lat[ts_mask], ts_seg.lon[ts_mask],
                )
                enc_type = classify_colreg_encounter(
                    mean_bearing,
                    np.mean(ts_seg.heading_deg[ts_mask]),
                    np.mean(os_seg.heading_deg[os_mask]),
                )

                encounters.append(Encounter(
                    own_mmsi=os_seg.mmsi, target_mmsi=ts_seg.mmsi,
                    encounter_type=enc_type,
                    min_dcpa_m=min_dcpa, min_tcpa_s=tcpa_at_min,
                    t_encounter_start=t_start, t_encounter_end=t_end,
                    own_trajectory=_segment_to_array(os_seg, os_mask),
                    target_trajectory=_segment_to_array(ts_seg, ts_mask),
                ))

    return encounters
```

### 3.6 Stage 5 — maritime-schema YAML 导出

```python
# scenario_authoring/pipeline/stage5_export.py
import hashlib
import yaml
from pathlib import Path

OUTPUT_DIR = Path("scenarios/ais_derived")


def export_encounter_to_yaml(
    encounter: Encounter,
    geo_origin: tuple[float, float],
    output_dir: Path = OUTPUT_DIR,
    raw_csv_path: Path | None = None,
    csv_start_row: int = 0,
    csv_end_row: int = 0,
) -> Path:
    """Export a single AIS-derived encounter as maritime-schema TrafficSituation YAML.

    Returns path to the written YAML file.
    """
    from scenario_authoring.authoring.ais_source import ne_to_latlon

    # ── Compute traceability hash ─────────────────────────────────
    ais_source_hash = ""
    if raw_csv_path and raw_csv_path.exists():
        import pandas as pd
        df_slice = pd.read_csv(raw_csv_path, skiprows=csv_start_row,
                               nrows=csv_end_row - csv_start_row)
        content = df_slice.to_csv(index=False).encode('utf-8')
        ais_source_hash = hashlib.sha256(content).hexdigest()

    # ── Own-ship state at encounter start ─────────────────────────
    os_traj = encounter.own_trajectory
    ts_traj = encounter.target_trajectory
    os_lat, os_lon = ne_to_latlon(geo_origin, os_traj[0, 2], os_traj[0, 1])
    ts_lat, ts_lon = ne_to_latlon(geo_origin, ts_traj[0, 2], ts_traj[0, 1])

    scenario_id = (
        f"ais-{encounter.target_mmsi}-"
        f"{_ts_to_iso(encounter.t_encounter_start)}-"
        f"{encounter.encounter_type.lower()}-v1.0"
    )

    doc = {
        "title": f"AIS-derived {encounter.encounter_type} encounter — "
                 f"MMSI {encounter.target_mmsi}",
        "description": (
            f"Extracted from AIS data. "
            f"min DCPA={encounter.min_dcpa_m:.0f}m, "
            f"min TCPA={encounter.min_tcpa_s:.0f}s"
        ),
        "start_time": _ts_to_iso(encounter.t_encounter_start),
        "own_ship": {
            "id": "os",
            "nav_status": 0,
            "mmsi": encounter.own_mmsi,
            "initial": {
                "position": {"latitude": os_lat, "longitude": os_lon},
                "cog": float(os_traj[0, 4]),
                "sog": float(os_traj[0, 3]),
                "heading": float(os_traj[0, 4]),
            },
        },
        "target_ships": [{
            "id": f"ts-{encounter.target_mmsi}",
            "nav_status": 0,
            "mmsi": encounter.target_mmsi,
            "initial": {
                "position": {"latitude": ts_lat, "longitude": ts_lon},
                "cog": float(ts_traj[0, 4]),
                "sog": float(ts_traj[0, 3]),
                "heading": float(ts_traj[0, 4]),
            },
        }],
        "metadata": {
            "schema_version": "2.0",
            "scenario_id": scenario_id,
            "scenario_source": "ais_derived",       # ← triggers config-driven L1 mode
            "ais_source_hash": ais_source_hash,
            "ais_mmsi": [encounter.own_mmsi, encounter.target_mmsi],
            "ais_time_window": {
                "start": _ts_to_iso(encounter.t_encounter_start),
                "end": _ts_to_iso(encounter.t_encounter_end),
            },
            "vessel_class": "FCB",
            "odd_zone": "A",
            "geo_origin": {
                "latitude": geo_origin[0],
                "longitude": geo_origin[1],
            },
            "encounter": {
                "rule": _encounter_type_to_rule(encounter.encounter_type),
                "give_way_vessel": "unknown",
            },
            "disturbance_model": {
                "wind_kn": 0.0, "wind_dir_nav_deg": 0.0,
                "current_kn": 0.0, "current_dir_nav_deg": 0.0,
                "vis_m": 10000.0, "wave_height_m": 0.0,
            },
            "pass_criteria": {
                "max_dcpa_no_action_m": DCPA_THRESHOLD_M,
                "min_dcpa_with_action_m": DCPA_THRESHOLD_M,
            },
            "simulation": {
                "duration_s": encounter.t_encounter_end - encounter.t_encounter_start,
                "dt_s": 0.02,
                "n_rps_initial": 3.5,
            },
            "prng_seed": None,
        },
    }

    output_dir.mkdir(parents=True, exist_ok=True)
    yaml_path = output_dir / f"{scenario_id}.yaml"
    with open(yaml_path, 'w') as f:
        yaml.dump(doc, f, default_flow_style=False, sort_keys=False)

    # Cerberus validation
    from tools.sil.cerberus_validator import validate_yaml
    validate_yaml(doc)  # raises ValueError on failure

    return yaml_path


def _encounter_type_to_rule(enc_type: str) -> str:
    return {"HEAD_ON": "Rule14", "CROSSING": "Rule15",
            "OVERTAKING": "Rule13"}.get(enc_type, "Unknown")
```

---

## §4 ais_replay_node — 50Hz ROS2 回放节点

### 4.1 设计

```python
# scenario_authoring/replay/ais_replay_node.py
"""
ROS2 node that replays AIS-derived target ship trajectories at 50 Hz.

Input:  maritime-schema YAML file (ais_derived type) — loads ScenarioSpec
        to extract own-ship + target-ship trajectory arrays.
Output: /world_model/tracks (TrackedTargetArray) @ 50 Hz.

Interpolation: 2 Hz AIS → 50 Hz via linear interpolation (position/SOG)
               + unwrap → interp → re-wrap (COG).
"""
import rclpy
from rclpy.node import Node
from l3_external_msgs.msg import TrackedTargetArray
from scenario_authoring.replay.target_modes import TargetShipReplayer


class AisReplayNode(Node):
    def __init__(self, yaml_path: str) -> None:
        super().__init__("ais_replay_node")
        self._pub = self.create_publisher(TrackedTargetArray, "/world_model/tracks", 10)
        self._replayer = TargetShipReplayer.from_yaml(yaml_path)
        self._dt = 1.0 / 50.0  # 50 Hz
        self._timer = self.create_timer(self._dt, self._publish_tick)
        self._t_elapsed = 0.0
        self.get_logger().info(f"ais_replay_node started — {yaml_path}")

    def _publish_tick(self) -> None:
        msg = self._replayer.get_targets_at(self._t_elapsed)
        self._pub.publish(msg)
        self._t_elapsed += self._dt
```

### 4.2 2Hz→50Hz 插值（在 TargetShipReplayer 内）

对每条 target ship trajectory 预计算 50Hz 插值数组（初始化时一次完成）：

```python
def _interpolate_trajectory_50hz(traj: np.ndarray, dt: float = 0.02) -> np.ndarray:
    """traj: (N, 5) — t, lat, lon, sog, cog. Returns (M, 5) at 50 Hz."""
    t_orig = traj[:, 0]
    t_new = np.arange(t_orig[0], t_orig[-1], dt)

    lat_new = np.interp(t_new, t_orig, traj[:, 1])
    lon_new = np.interp(t_new, t_orig, traj[:, 2])
    sog_new = np.interp(t_new, t_orig, traj[:, 3])

    cog_rad = np.radians(traj[:, 4])
    cog_unwrapped = np.unwrap(cog_rad)
    cog_interp_rad = np.interp(t_new, t_orig, cog_unwrapped)
    cog_new = np.degrees(cog_interp_rad) % 360.0

    return np.column_stack([t_new, lat_new, lon_new, sog_new, cog_new])
```

---

## §5 target_modes — 3 种 Target 运动模式

### 5.1 接口定义

```python
# scenario_authoring/replay/target_modes.py
from abc import ABC, abstractmethod
import numpy as np


class TargetShipReplayer(ABC):
    """Abstract interface for target ship trajectory provision at 50 Hz.

    D1.3b.2 Phase 1 only implements AisReplayVessel.
    NcdmVessel (D2.4) and IntelligentVessel (D3.6) are deferred.
    """

    @abstractmethod
    def get_targets_at(self, t_s: float) -> "TrackedTargetArray":
        """Return TrackedTargetArray for all targets at time t_s (seconds from
        scenario start). Called by ais_replay_node timer at 50 Hz.
        """
        ...

    @classmethod
    def from_yaml(cls, yaml_path: str) -> "TargetShipReplayer":
        """Factory: determine mode from scenario_source in YAML metadata."""
        ...


class AisReplayVessel(TargetShipReplayer):
    """Replay historical AIS trajectory at 50 Hz (D1.3b.2 Phase 1)."""

    def __init__(self, trajectory: np.ndarray): ...
    def get_targets_at(self, t_s: float) -> "TrackedTargetArray": ...


class NcdmVessel(TargetShipReplayer):
    """NCDM Ornstein-Uhlenbeck stochastic prediction (D2.4). STUB ONLY."""

    def __init__(self): raise NotImplementedError("D2.4")
    def get_targets_at(self, t_s: float): raise NotImplementedError("D2.4")


class IntelligentVessel(TargetShipReplayer):
    """VO/MPC multi-agent interactive target (D3.6). STUB ONLY."""

    def __init__(self): raise NotImplementedError("D3.6")
    def get_targets_at(self, t_s: float): raise NotImplementedError("D3.6")
```

### 5.2 Factory dispatch

```python
@classmethod
def from_yaml(cls, yaml_path: str) -> "TargetShipReplayer":
    """Dispatch based on target_ships[*].model field in YAML."""
    import yaml
    with open(yaml_path) as f:
        data = yaml.safe_load(f)
    model = data.get("target_ships", [{}])[0].get("model", "ais_replay_vessel")
    if model == "ais_replay_vessel":
        return AisReplayVessel.from_yaml_data(data)
    elif model == "ncdm_vessel":
        return NcdmVessel()  # stub — D2.4
    elif model == "intelligent_vessel":
        return IntelligentVessel()  # stub — D3.6
    raise ValueError(f"Unknown target model: {model}")
```

---

## §6 L1 Mode Switching（配置驱动）

### 6.1 模式路由逻辑

colav-simulator 风格：模式不通过 launch arg 显式传递，而是从场景 YAML 推断。

```python
# scenario_authoring/launch/scenario_authoring.launch.py

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, LogInfo
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PythonExpression
from launch_ros.actions import Node
import yaml


def generate_launch_description() -> LaunchDescription:
    scenario_yaml = LaunchConfiguration("scenario_yaml", default="")

    # Load metadata.scenario_source from YAML to determine mode
    is_ais_derived = PythonExpression([
        "'ais_derived' in open('", scenario_yaml, "').read()"
    ])

    return LaunchDescription([
        DeclareLaunchArgument("scenario_yaml", default_value=""),

        # synthetic mode (default): sil_mock_publisher
        Node(
            package="sil_mock_publisher",
            executable="sil_mock_node",
            name="sil_mock_publisher",
            output="screen",
            condition=IfCondition(PythonExpression(["not ", is_ais_derived])),
        ),

        # ais_replay mode: scenario_authoring replay node
        Node(
            package="scenario_authoring",
            executable="ais_replay_node",
            name="ais_replay_node",
            output="screen",
            parameters=[{"yaml_path": scenario_yaml}],
            condition=IfCondition(is_ais_derived),
        ),
    ])
```

**语义**：
- 不传 `scenario_yaml` → sil_mock_node（保持 D1.3b.1 现有行为零改动）
- 传 `scenario_yaml` + `metadata.scenario_source == "ais_derived"` → ais_replay_node
- 传 `scenario_yaml` + 其他 scenario_source → sil_mock_node（Imazu/自编场景照常走 synthetic）

rosbag 模式接口预留：`l1_mode` arg 在 launch file 中声明但不使用（stub），D2.5 实现后再激活。

---

## §7 数据源 — Kystverket / NOAA 下载

### 7.1 Kystverket AIS 数据

- **URL**：https://kystverket.no/en/navigation-and-monitoring/ais/ais-data-for-research/
- **License**：Norwegian Licence for Open Government Data (NLOD) — 等同于 CC BY 4.0
- **格式**：CSV（逗号分隔，含 MMSI/timestamp/lat/lon/sog/cog/heading/ship_type）
- **覆盖范围**：挪威沿海，Trondheim Fjord 区域 AIS 密度高

### 7.2 NOAA MarineCadastre AIS 数据

- **URL**：https://marinecadastre.gov/accessais/
- **License**：CC0 1.0（公共领域）
- **格式**：ZIP 压缩 CSV
- **覆盖范围**：美国近海，San Francisco Bay 区域高密度航运

### 7.3 下载脚本（模板）

```python
# scripts/download_kystverket.py
"""Download Kystverket AIS data for research use.

License: NLOD (Norwegian Licence for Open Government Data)
URL: https://kystverket.no/en/navigation-and-monitoring/ais/ais-data-for-research/
"""
import requests
from pathlib import Path

KYSTVERKET_BASE_URL = "https://ais.kystverket.no/ais-data/"
OUTPUT_DIR = Path("data/ais_datasets/kystverket")


def download_file(year: int, month: int, day: int) -> Path:
    """Download a single day's AIS data CSV."""
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    filename = f"ais_{year}{month:02d}{day:02d}.csv"
    url = f"{KYSTVERKET_BASE_URL}{filename}"
    resp = requests.get(url, stream=True, timeout=300)
    resp.raise_for_status()
    output_path = OUTPUT_DIR / filename
    with open(output_path, 'wb') as f:
        for chunk in resp.iter_content(chunk_size=8192):
            f.write(chunk)
    return output_path
```

### 7.4 数据集管理

下载后 CSV 存入 `data/ais_datasets/kystverket/` 和 `data/ais_datasets/noaa/`。`.gitignore` 已排除 `data/ais_datasets/`（二进制/大文件不入 git）。下载脚本的 SHA256 输出记录到 `data/ais_datasets/MANIFEST.txt`，供 CCS 审计。

---

## §8 encounter_extractor — 管线编排器

```python
# scenario_authoring/authoring/encounter_extractor.py
from pathlib import Path
from scenario_authoring.pipeline.stage1_group import group_by_mmsi
from scenario_authoring.pipeline.stage2_resample import split_into_segments
from scenario_authoring.pipeline.stage3_smooth import smooth_segment
from scenario_authoring.pipeline.stage4_filter import filter_encounters
from scenario_authoring.pipeline.stage5_export import export_encounter_to_yaml
from scenario_authoring.authoring.ais_source import load_ais_dataset


def extract_encounters(
    data_dir: Path,
    geo_origin: tuple[float, float] = (63.0, 5.0),
    own_mmsi: int | None = None,
) -> list[Path]:
    """Full pipeline: raw AIS → maritime-schema YAML files.

    1. Load all AIS records from data_dir
    2. Stage 1: Group by MMSI
    3. Stage 2: Split into continuous segments, resample to 0.5s
    4. Stage 3: Savitzky-Golay smooth SOG/COG
    5. Stage 4: DCPA/TCPA filter + COLREG classify
    6. Stage 5: Export as maritime-schema YAML

    Returns list of YAML file paths.
    """
    records = load_ais_dataset(data_dir)

    if own_mmsi is not None:
        own_records = [r for r in records if r.mmsi == own_mmsi]
        target_records = [r for r in records if r.mmsi != own_mmsi]
    else:
        # Without an identified own-ship, treat vessel with most records as OS
        from collections import Counter
        mmsi_counts = Counter(r.mmsi for r in records)
        own_mmsi = mmsi_counts.most_common(1)[0][0]
        own_records = [r for r in records if r.mmsi == own_mmsi]
        target_records = [r for r in records if r.mmsi != own_mmsi]

    # Stage 1
    target_groups = group_by_mmsi(target_records)
    own_segments_raw = group_by_mmsi(own_records).get(own_mmsi, [])

    # Stage 2
    all_own_segments = split_into_segments(own_segments_raw)
    all_target_segments = []
    for mmsi, recs in target_groups.items():
        all_target_segments.extend(split_into_segments(recs))

    # Stage 3
    all_own_segments = [smooth_segment(s) for s in all_own_segments]
    all_target_segments = [smooth_segment(s) for s in all_target_segments]

    # Stage 4
    encounters = filter_encounters(all_own_segments, all_target_segments)

    # Stage 5
    yaml_paths = []
    for i, enc in enumerate(encounters):
        raw_csv = data_dir / (list(data_dir.glob("*.csv"))[0])  # reference for hash
        yp = export_encounter_to_yaml(
            enc, geo_origin=geo_origin,
            raw_csv_path=raw_csv,
            csv_start_row=0, csv_end_row=0,  # TODO: track row ranges through pipeline
        )
        yaml_paths.append(yp)

    return yaml_paths
```

---

## §9 ais_source — 数据源适配 + 坐标变换

```python
# scenario_authoring/authoring/ais_source.py
from pathlib import Path
from ais_bridge.nmea_decoder import AISRecord, decode_file
from ais_bridge.dataset_loader import load_noaa_csv
import pyproj


# ── Coordinate Transform ──────────────────────────────────────
_WGS84 = "EPSG:4326"


def latlon_to_utm_zone() -> int:
    """Return UTM zone from geo_origin longitude (auto-detect)."""
    ...


def latlon_to_ne(lat: np.ndarray, lon: np.ndarray,
                 utm_zone: int = 33) -> tuple[np.ndarray, np.ndarray]:
    """WGS84 → NE Cartesian via UTM projection."""
    proj_str = f"+proj=utm +zone={utm_zone} +datum=WGS84 +units=m"
    transformer = pyproj.Transformer.from_crs(_WGS84, proj_str, always_xy=True)
    e, n = transformer.transform(lon, lat)
    return n, e


def ne_to_latlon(geo_origin: tuple[float, float],
                 n_m: float, e_m: float,
                 utm_zone: int = 33) -> tuple[float, float]:
    """NE Cartesian → WGS84."""
    origin_n, origin_e = latlon_to_ne(
        np.array([geo_origin[0]]), np.array([geo_origin[1]]), utm_zone
    )
    proj_str = f"+proj=utm +zone={utm_zone} +datum=WGS84 +units=m"
    transformer = pyproj.Transformer.from_crs(proj_str, _WGS84, always_xy=True)
    lon, lat = transformer.transform(origin_e + e_m, origin_n + n_m)
    return lat, lon


# ── Data Loader ────────────────────────────────────────────────

def load_ais_dataset(data_dir: Path) -> list[AISRecord]:
    """Load all AIS data from a directory (CSV or NMEA files)."""
    records = []
    for csv_file in data_dir.glob("*.csv"):
        records.extend(load_noaa_csv(str(csv_file)))
    for nmea_file in data_dir.glob("*.nmea"):
        records.extend(decode_file(str(nmea_file)))
    return records
```

---

## §10 Task 拆分（每 task ≤ 4h，24 tasks，~160h = 4.0 人周）

### Track A：数据集 + 数据源适配（5/25–5/30，~24h）

| ID | Task | 工时 | 前置 | AC |
|---|---|---|---|---|
| **T1** | `download_kystverket.py` 脚本 + 实际下载 ≥1h Trondheim 区域 AIS 数据 | 4h | — | ≥1h CSV 文件存在，≥50 MMSI，文件大小 < 500MB |
| **T2** | `download_noaa.py` 脚本 + 实际下载 ≥1h SF Bay AIS 数据 | 4h | — | ≥1h CSV 存在，≥100 MMSI（SF Bay 高密度） |
| **T3** | 数据集格式审计：Kystverket/NOAA CSV 字段确认 + pyproj 环境验证 | 2h | T1, T2 | 审计报告 commit 到 spec §1.3；pyproj 可正常 import |
| **T4** | `ais_source.py` 数据源适配器（Kystverket CSV + NOAA CSV 统一 `load_ais_dataset()`） | 3h | T3 | `load_ais_dataset()` 对两种数据集均返回有效 list[AISRecord] |
| **T5** | `ais_source.py` 坐标变换（latlon_to_ne + ne_to_latlon via pyproj） | 3h | T3 | round-trip 误差 < 1m；自适应 UTM zone 正确 |
| **T6** | `TimedAISRecord` 扩展 + `from_ais_record()` 工厂方法（从 pyais rx_time / NOAA BaseDateTime 提取时间戳） | 4h | T4 | 对 Kystverket + NOAA 两种数据集时间戳解析率 ≥ 95% |
| **T7** | 数据集验证脚本：`scripts/validate_dataset.py`（MMSI 数 / 时间跨度 / 格式完整性） | 4h | T1, T2, T6 | 对两个数据集输出统计摘要，commit 到 spec |

### Track B：5 阶段管线（5/31–6/10，~48h）

| ID | Task | 工时 | 前置 | AC |
|---|---|---|---|---|
| **T8** | `scenario_authoring` 包脚手架（setup.py + package.xml + 目录结构 + pyproj/scipy 依赖） | 3h | — | `colcon build --packages-select scenario_authoring` 成功 |
| **T9** | `stage1_group.py` 实现 + 单元测试（3 组 MMSI，含 <10 records 过滤验证） | 4h | T6, T8 | test 通过；输出 dict key 全为 MMSI ≥10 records 的船 |
| **T10** | `stage2_resample.py` 实现（间隙拆段 + Δt=0.5s 重采样 + COG unwrap） | 6h | T9 | test: 间隙 10min → 正确拆段；COG 跨 0° 时插值无跳变 |
| **T11** | `stage3_smooth.py` 实现（Savitzky-Golay） + 单元测试 | 4h | T10 | test: SOG 平滑后 std 降低；COG 平滑后无 360° 跳变 |
| **T12** | `stage4_filter.py` DCPA/TCPA 计算实现（相对运动法） + 单元测试 | 6h | T10 | test: 已知 CPA 场景验证误差 < 10m；接近/远离判断正确 |
| **T13** | `stage4_filter.py` COLREG 几何分类 + encounter 过滤整合 | 4h | T12 | test: HEAD_ON/CROSSING/OVERTAKING 各 ≥1 个正确分类 |
| **T14** | `stage5_export.py` maritime-schema YAML 导出 + Cerberus 验证 | 5h | T13 | 产出 YAML 通过 `cerberus_validator.validate_yaml()`；`ais_source_hash` 可重现验证 |
| **T15** | `encounter_extractor.py` 管线编排器（调 stage1→5 全流程） | 4h | T9-T14 | 端到端：原始 AIS CSV → list[YAML paths]；≥1 个 YAML Cerberus 通过 |
| **T16** | 端到端验证：1h Kystverket 数据 → 提取 encounter → `assert len(encounters) >= 10`（DoD threshold） | 4h | T15 | 终端输出 "Extracted N encounters from 1h Kystverket data (N >= 10)" |
| **T17** | 管线批量验证：10 个 encounter YAML 全量 Cerberus + C++ 双语言验证通过 | 4h | T16 | Python + C++ cerberus 验证全部通过 |

### Track C：ais_replay_node + target_modes（6/11–6/20，~32h）

| ID | Task | 工时 | 前置 | AC |
|---|---|---|---|---|
| **T18** | `target_modes.py` 接口定义（TargetShipReplayer ABC + 3 种 stub） | 3h | T8 | ABC 含 `get_targets_at()` 抽象方法；NcdmVessel/IntelligentVessel 抛 NotImplementedError |
| **T19** | `ais_replay_node.py` — ROS2 节点骨架 + `from_yaml()` 工厂方法 | 4h | T18 | `ros2 run scenario_authoring ais_replay_node` 启动无报错 |
| **T20** | `ais_replay_node.py` — 50Hz 插值（2Hz→50Hz linear） + TrackedTargetArray 发布 | 6h | T19 | `ros2 topic hz /world_model/tracks` ≈ 50 Hz；lat/lon 在合理范围 |
| **T21** | `ais_replay_node.py` — M2 World Model TrackedTarget 字段对齐验证 | 3h | T20 | schema_version='v1.1.2'；lat/lon/sog/cog/heading 均非零；M2 负责人签字 |
| **T22** | 50Hz 延迟评估（ROS2 Timer Python 精度 ~10ms） + 降级方案文档 | 3h | T20 | 实测 hz 统计（mean/std）；若 < 45Hz 写入 contingency 降级方案 |
| **T23** | `scenario_authoring.launch.py` 配置驱动 mode switching（per §6） | 4h | T19, T14 | synthetic YAML → sil_mock_node；ais_derived YAML → ais_replay_node；不传参数 → sil_mock_node |

### Track D：集成 + DoD 全闭（6/21–7/15，~56h 含缓冲）

| ID | Task | 工时 | 前置 | AC |
|---|---|---|---|---|
| **T24** | DEMO-2 端到端预演：SF Bay AIS → extract 1 encounter → YAML → SIL run → ais_replay 吻合（误差 < 5m） | 8h | T23, T17 | DEMO-2 Charter 全流程跑通；HMI 海图同步显示 |
| **T25** | 全量回归：pytest + colcon test + cerberus 双语言 + lint | 4h | T24 | 全部 exit 0 |
| **T26** | Finding 关闭：G P0-G-1(c) CLOSED + spec 更新 DoD checkboxes | 2h | T25 | `00-consolidated-findings.md` 更新；spec 全闭 ✅ |
| **T27** | 文档：encounter YAML 格式说明 + 使用指南（README in scenario_authoring/） | 2h | T26 | README 含 1 个完整 example |
| **T28** | Buffer / contingency（R-C1–R-C5 触发时消化） | 40h | — | 覆盖数据集格式不兼容 / encounter < 10 / 50Hz 不稳等风险 |

**总计：24+48+32+56 = ~160h（4.0 人周）。缓冲 40h 含在 Track D 内。**

---

## §11 每 Task Acceptance Criteria 汇总

| ID | Acceptance Criteria |
|---|---|
| T1 | ≥1h Kystverket CSV 存在；≥50 MMSI；文件大小 < 500MB；license 确认 |
| T2 | ≥1h NOAA CSV 存在；≥100 MMSI（SF Bay 高密度）；license 确认 |
| T3 | CSV 字段列名确认文档；pyproj.transform 测试通过 |
| T4 | `load_ais_dataset()` 对 Kystverket + NOAA 均返回 list[AISRecord] |
| T5 | round-trip (lat,lon)→NE→(lat,lon) 误差 < 1m |
| T6 | 时间戳解析率 ≥ 95%（两种格式）；无法解析的行记录到 warn log |
| T7 | 统计摘要输出：MMSI 数量 / 时间跨度 / 缺失字段百分比 |
| T8 | `colcon build` 成功；`python -c "import scenario_authoring"` 无报错 |
| T9 | ≥3 MMSI 分组正确；<10 records 的船被正确丢弃；时间排序正确 |
| T10 | test 含 10min gap 拆段 + COG=359°→1° unwrap 插值正确 |
| T11 | SOG std 平滑后降低；COG 平滑后无 NaN |
| T12 | 已知 CPA=0 场景 DCPA≈0；已知 CPA=1000m 场景 DCPA≈1000 ±10m |
| T13 | HEAD_ON(bearing<6°, hdg_diff>170°)；OVERTAKING(bearing∈[112.5°,247.5°])；CROSSING(其余) 各 1 正确分类 |
| T14 | `validate_yaml(doc)` 不抛异常；SHA256 可重现（同一输入同 hash） |
| T15 | 端到端：AIS CSV → ≥1 个 YAML 路径；日志打印各 stage 耗时 |
| T16 | 1h Kystverket → ≥10 encounters (DCPA<500m) |
| T17 | Python + C++ cerberus 对 10 个 YAML 全 PASS |
| T18 | ABC 含 `get_targets_at()`；NcdmVessel/IntelligentVessel 构造时抛 NotImplementedError，消息含 "D2.4" / "D3.6" |
| T19 | `ros2 run scenario_authoring ais_replay_node` 无 import 错误 |
| T20 | `ros2 topic hz /world_model/tracks` ≈ 50 Hz；schema_version='v1.1.2' |
| T21 | M2 负责人确认 TrackedTarget 字段全对齐；source_sensor='ais'；covariance 非单位阵 |
| T22 | 实测 hz mean ≥ 48 Hz（Python 节点）；若 < 45Hz 输出降级评估文档 |
| T23 | `scenario_yaml`=ais_derived → ais_replay_node；=imazu → sil_mock_node；不传 → sil_mock_node |
| T24 | SF Bay real AIS encounter → SIL run → ais_replay 位置误差 < 5m（vs 原始 AIS 数据） |
| T25 | pytest + colcon test + cerberus (Py+C++) + ruff 全部 0 violations |
| T26 | G P0-G-1(c) 标 CLOSED；spec DoD §13 全 ✅ |
| T27 | README 含 `python -m scenario_authoring.authoring.encounter_extractor --data-dir data/ais_datasets/kystverket` 示例 |
| T28 | 至少 1 个 contingency 场景被演习（dry-run 验证 contingency 有效性） |

---

## §12 Risk + Contingency

| ID | 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|---|
| **R-C1** | Kystverket 数据格式与预期不符（非标准 CSV / XML 包装） | 中 | 高（T3-T7 阻塞） | T3 首次下载后立即审计格式；若不兼容，切换为 NOAA 单源（≥2h NOAA 替代 Kystverket）；T2 NOAA 同步下载作 fallback |
| **R-C2** | DCPA 过滤后有效 encounter < 10 | 中 | 高（DoD 不闭） | 调低 DCPA 阈值 500m→800m 或扩大时间窗 TCPA 20min→30min；连续下降直到 encounter 数 ≥10；若仍失败，扩大数据时间范围至 3h |
| **R-C3** | 50Hz Python 节点延迟 > 20ms（ROS2 Timer 精度 ~10ms） | 中 | 中（ais_replay 不达 50Hz） | T22 实测统计；若 < 45Hz 降级方案：(a) 用 ros2 rate 控制代替 Timer；(b) 终极降级：C++ 节点（Phase 2 追加 1pw）；Phase 1 可用 25Hz 通过 DoD（架构 F.7 未硬编码 50Hz） |
| **R-C4** | maritime-schema 字段与 D1.3b.1 最终版不兼容 | 低 | 中 | W2 末（5/31）与 D1.3b.1 owner 对齐 `metadata.*` 字段列表；cerberus `allow_unknown=True` 天然缓冲 |
| **R-C5** | Savitzky-Golay window/order 参数对 Kystverket 数据不适配（过度平滑或振荡） | 低 | 低 | T11 测试中对比平滑前后 DCPA 计算值，差异应 < 5m；若差异大，调 window 21→11 或 order 3→2 |
| **R-C6** | pyproj 在 ROS2 Humble 容器中缺失 | 低 | 中 | T3 验证容器内 `import pyproj`；若缺失，`apt install python3-pyproj` 加入 Dockerfile；备选：复制 D1.3b.1 的 flat-earth geo_utils.py（限制使用范围 < 50km） |

---

## §13 D1.3b.2 全闭判据（7/15 EOD）

- [ ] `src/sim_workbench/scenario_authoring/` 包存在，`colcon build` 成功
- [ ] 1h Kystverket 真实 AIS 数据本地存储（CSV，≥50 MMSI）+ NOAA SF Bay 数据本地存储（CSV，≥100 MMSI）
- [ ] AIS 预处理管线：1h 数据 → 提取 ≥10 个有效 encounter（DCPA < 500m，TCPA < 20min）
- [ ] 单 encounter → maritime-schema YAML 导出 + Python Cerberus 验证通过 + C++ cerberus-cpp 验证通过
- [ ] YAML 含 `metadata.scenario_source: "ais_derived"`、`metadata.ais_source_hash`（SHA256）、`metadata.ais_mmsi[]`、`metadata.ais_time_window`
- [ ] `ais_replay_vessel` 模式实装 + 50Hz 实时位置发布到 `/world_model/tracks` topic（实测 ≥ 48Hz）
- [ ] 配置驱动 mode switching：`scenario_source == "ais_derived"` → ais_replay_node / 其他 → sil_mock_node
- [ ] `ncdm_vessel` + `intelligent_vessel` 接口 stub（抛 NotImplementedError，消息含目标 D 编号）
- [ ] 关闭 finding ID：G P0-G-1(c)（v3.1 NEW）
- [ ] 全量回归：pytest + colcon test + cerberus (Py+C++) + ruff 全部 0 violations
- [ ] `ais_bridge` / `sil_mock_publisher` / `fcb_simulator` 零修改（git diff 验证）

---

## §14 依赖图

```
T1 (Kystverket) ──┬── T3 (审计) ── T4 (ais_source adapter) ── T5 (pyproj) ── T6 (TimedAISRecord)
T2 (NOAA) ────────┘                                                │
                                                                   ▼
T7 (数据集验证) ──────────────────────────────── T8 (包脚手架) ── T9 (stage1) ── T10 (stage2)
                                                                                       │
                                                                      ┌────────────────┘
                                                                      ▼
                                                       T11 (stage3 smooth) ── T12 (DCPA) ── T13 (COLREG classify)
                                                                                              │
                                                                                              ▼
                                                                              T14 (stage5 export) ── T15 (extractor) ── T16 (end2end 10 encounters)
                                                                                                                         │
                                                                                                                         ▼
                                                                                                              T17 (batch cerberus)


T8 ── T18 (target_modes ABC) ── T19 (replay node skeleton) ── T20 (50Hz interp) ── T21 (M2 对齐) ── T22 (延迟评估)
                                                                     │
                                                                     ▼
                                                          T23 (launch mode switching)
                                                                     │
                                                          T15 + T23 ── T24 (DEMO-2 dry run) ── T25 (全量回归) ── T26 (finding 关闭) ── T27 (文档)
```

**可并行**：Track A（数据集）→ Track B（管线）和 Track C（replay node）在 T8 之后完全并行；Track C 不需要管线完整（仅需 T14 的 YAML 输出格式 + T18 的 ABC 接口）。

---

## §15 Owner-by-Day 矩阵（5/25–7/15，4.0 人周）

| 周 | 日期 | role-hat | 预计 task | 备注 |
|---|---|---|---|---|
| W0 | 5/25–5/30 | 技术负责人 | T1, T2, T3, T4, T5 | 数据集下载（可能需等网络） |
| W0 | 5/31 | 技术负责人 + D1.3b.1 owner | — | **W2 末对齐节点**：确认 maritime-schema metadata 字段 |
| W1 | 6/1–6/6 | 技术负责人 | T6, T7, T8, T9, T10 | pipeline stage 1-2 |
| W2 | 6/7–6/13 | 技术负责人 | T11, T12, T13, T14 | pipeline stage 3-5 |
| W2 | 6/14 | 技术负责人 + V&V 工程师 | T15, T16, T17 | 端到端验证 + V&V 审查 encounter 质量 |
| W3 | 6/15 | 全员 | **🎬 DEMO-1** | D1.3b.2 不参与 DEMO-1（仅 DEMO-2） |
| W3 | 6/16–6/20 | 技术负责人 | T18, T19, T20, T21 | replay node |
| W4 | 6/21–6/25 | 技术负责人 | T22, T23, T24 (部分) | 延迟评估 + launch + DEMO-2 预演开始 |
| W5 | 6/26–7/10 | 技术负责人 + M2 负责人 | T24 (完成), T25, T26 | DEMO-2 全流程 dry run + 回归 |
| W6 | 7/11–7/15 | 技术负责人 | T27, T28 (buffer) | 文档 + contingency 消化 |

**7/15 EOD**：D1.3b.2 正式关闭（比 DEMO-2 7/31 提前 2 周，留 buffer 给集成问题）。

---

## §16 与下游 D 的接口

| 下游 D | 消费 D1.3b.2 产出 | 接口契约 |
|---|---|---|
| **D1.3b.1** YAML 场景管理 | `scenarios/ais_derived/*.yaml` | maritime-schema v2.0 + cerberus 验证通过；`metadata.scenario_source: "ais_derived"` 用于区分来源 |
| **D1.3b.3** DEMO-2 Web HMI | `/world_model/tracks` (TrackedTargetArray @ 50Hz) + `scenarios/ais_derived/*.yaml` | HMI 加载 AIS-derived YAML，ais_replay_node 提供 50Hz 目标数据流 |
| **D2.4** M6 COLREGs Reasoner | `ncdm_vessel` 接口 stub | `TargetShipReplayer` ABC；D2.4 实现 `NcdmVessel` 类，override `get_targets_at()` |
| **D2.5** SIL 集成 | `ais_replay_node` 50Hz topic + L1 mode switching 架构 | D2.5 加 `rosbag` mode，在 `from_yaml()` factory 增加一条分支 |
| **D3.6** SIL 1000+ Monte Carlo | `intelligent_vessel` 接口 stub + encounter extractor 管线 | D3.6 实现 `IntelligentVessel` 类；管线作为批量场景生成工具被 D3.6 调用 |

---

## §17 来源与置信度

| 断言 | 来源 | 置信度 |
|---|---|---|
| colav-simulator 包结构（单包，子模块按域划分，scenario_generator 顶层独立，配置驱动模式切换） | 本地仓库 `/Users/marine/Code/colav-simulator/colav_simulator/` + config/simulator.yaml | 🟢 High |
| Kystverket AIS 开放数据 NLOD license | kystverket.no 官网；挪威开放数据政策 | 🟢 High |
| NOAA MarineCadastre AIS CC0 1.0 | D1.3a spec §4.2 已确认；marinecadastre.gov | 🟢 High |
| DCPA/TCPA 相对运动法公式 | IMO 标准 + velocity obstacle 文献 | 🟢 High |
| Savitzky-Golay vs Kalman 平滑选择 | scipy 文档 + colav-simulator tracking/trackers.py Kalman 实现 ~300 行 | 🟢 High |
| pyproj UTM 坐标变换 | PROJ 官方文档；ROS2 Humble 间接依赖（geographic_msgs） | 🟢 High |
| cerberus `allow_unknown=True` 对 metadata.* 扩展字段兼容 | cerberus 官方文档 + D1.3b.1 spec §5.2 确认 | 🟢 High |
| ROS2 Python Timer 精度 ~10ms | ROS2 官方文档 rclpy Timer | 🟡 Medium（实测 T22 验证） |

---

*文档版本 v1.0 · 2026-05-11 · brainstorming 产出，待用户评审后走 /writing-plans → /executing-plans*
