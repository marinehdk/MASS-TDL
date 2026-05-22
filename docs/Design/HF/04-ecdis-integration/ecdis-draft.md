# ECDIS 集成草案 · D2.6

| 版本 | 2026-06-23 v0.1 |
|---|---|
| 范围 | 接口约束边界说明（Interface Draft）；**完整实现推 D3.4 M8 完整化** |
| 标准基线 | IHO S-100 框架 / IEC 61174:2015 ECDIS 操作及性能要求 |
| Affects | M8 §21 Web HMI / D2.8 v1.1.3 stub §21 |

## 1. 集成目标（D2.6 范围）

本草案仅定义 M8 Web HMI 与 ECDIS 图幅数据之间的**接口约束边界**：
- M8 以叠加层（overlay）形式在 MapLibre 底图上显示 ENC 相关符号
- 不实现 S-100 图幅渲染引擎（推 D3.4）

## 2. IHO S-100 关键约束

| 约束项 | S-100 参考 | M8 要求 | 状态 |
|---|---|---|---|
| 坐标参考系 | §4.1 WGS-84 | MapLibre EPSG:4326 → 直接兼容 | ✅ 无需转换 |
| 深度单位 | §4.3 meters (MLLW) | 显示层注明水深基准（来源：L2 ENC 元数据）| 🟡 待 L2 接口对齐 |
| 时间坐标 | §4.4 UTC | ROS2 `header.stamp` 统一 UTC | ✅ 已满足 |
| 物标分类 | S-57 对象目录 | 仅使用 COLAV 相关物标（≤20 类）| 🟡 D3.4 完整化 |

## 3. IEC 61174 操作约束

| IEC 61174 要求 | M8 响应 | 优先级 |
|---|---|---|
| §5.1.1 不遮挡安全相关信息 | M8 叠加层须可独立关闭（toggle）| Must（D3.4）|
| §7.3 告警层级（Class A/B/C）| M8 告警 topic 须标注 IEC 62288 class 字段 | Must（D3.4）|
| §7.4 位置更新频率 ≤1s | `/l3/tracks @10Hz` 已满足 | ✅ |
| §8.2 日夜模式 | MapLibre 支持主题切换；M8 须暴露 `dark_mode` toggle | Should（D3.4）|

## 4. M8 ECDIS 集成接口草案（ROS2 → MapLibre）

```yaml
# 接口草案（D2.6 占位；D3.4 实装时扩充）
topic: /l3/enc_overlay
message_type: sensor_msgs/CompressedImage   # 或 custom ENC tile
frequency_hz: 0.1                            # 底图更新率（静态为主）
source: L2 Voyage Planner ENC cache          # 上游：L2 提供 ENC tile
render_mode: maplibre_geojson                # 本期兼容格式
missing_fallback: openstreetmap_nautical     # D3.4 前的降级底图
```

## 5. 推迟到 D3.4 的完整实现

- S-100 图幅渲染引擎（libS100 或 web-based renderer）
- S-57 物标完整分类 + 颜色方案
- ECDIS SENC 文件导入 pipeline
- IEC 61174 §8.2 日夜模式完整实现
- IHO S-64 验证套件（ECDIS 型式认可前提）

## 6. D2.8 v1.1.3 stub 输入（§21）

本草案作为架构 v1.1.3 stub §21（Web HMI 接口契约）中"ECDIS 集成约束"的输入：
- §21.3 应引用本草案 §2/§3 的约束表
- §21.4 应包含本草案 §4 的接口 YAML

## 7. 参考文献

- IHO S-100 Ed.5.0.0 (2021) — Universal Hydrographic Data Model
- IEC 61174:2015 — ECDIS operational and performance requirements
- [R-IMO-SMODE] IMO MSC.1/Circ.1609 (2019) S-Mode
