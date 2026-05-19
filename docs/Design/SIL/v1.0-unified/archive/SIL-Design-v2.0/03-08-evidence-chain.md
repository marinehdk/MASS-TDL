# 认证证据链设计

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-DD-003-08 |
| 版本 | v2.0 |
| 日期 | 2026-05-13 |
| 父文档 | [03-sil-detailed-design.md](./03-sil-detailed-design.md) |
| 认证目标 | CCS i-Ship (Nx, Ri/Ai) + DNV-CG-0264 |

---

## 1. 认证目标概述

### 1.1 CCS 中国船级社

| 认证标识 | 说明 |
|---------|------|
| **i-Ship (Nx)** | 智能航行（自动驾驶）认证 |
| **Ri** | 船舶远程控制认证 |
| **Ai** | 船载人工智能认证 |

### 1.2 DNV 挪威船级社

| 规范 | 说明 |
|------|------|
| **DNV-CG-0264** | 自主和远程操作船舶 |
| **DNV-RP-0513** | 仿真模型保证 |
| **OSP** | 开放仿真平台互操作 |

---

## 2. 证据链架构

### 2.1 证据链设计原则

1. **可追溯性**：从场景定义到仿真结果全程可追溯
2. **不可篡改性**：关键数据 SHA256 签名
3. **完整性**：包含认证所需的所有证据
4. **可审计性**：格式标准化，便于审计

### 2.2 证据链流程

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                           证据链流程                                         │
│                                                                              │
│  ① 场景定义                      ② 仿真运行                    ③ 结果输出   │
│  ┌──────────────┐               ┌──────────────┐               ┌──────────┐│
│  │ scenario.yaml│               │ 50Hz 仿真    │               │Marzip   ││
│  │ (YAML)      │               │ 闭环运行     │               │证据包    ││
│  └──────┬──────┘               └──────┬──────┘               └────┬─────┘│
│         │                             │                             │       │
│         ▼                             ▼                             ▼       │
│  ┌──────────────┐               ┌──────────────┐               ┌──────────┐│
│  │SHA256 签名   │               │MCAP 录制     │               │Verdict   ││
│  │锁定场景内容  │               │完整轨迹      │               │PASS/FAIL││
│  └──────────────┘               └──────────────┘               └─────────┘│
│                                                                              │
│  证据关联                                                           │
│  scenario.sha256 ←→ results.bag.mcap.sha256 ←→ verdict.json               │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Marzip 证据包格式

### 3.1 包结构

```
{run_id}_evidence.marzip
├── scenario.yaml              ← maritime-schema 场景定义
├── scenario.sha256           ← 场景 SHA256 签名
├── results.arrow             ← KPI 列存表
├── results.bag.mcap          ← 完整 50Hz 录像
├── results.bag.mcap.sha256   ← MCAP SHA256 签名
├── asdr_events.jsonl         ← ASDR 事件流（人类可读）
├── verdict.json               ← PASS/FAIL + 理由 + KPI
└── manifest.yaml             ← 版本/作者/工具链/L3 kernel SHA
```

### 3.2 各文件说明

#### scenario.yaml
- **内容**：maritime-schema TrafficSituation 格式
- **用途**：场景定义、参数配置、初始状态
- **签名**：SHA256 签名，锁定内容

#### scenario.sha256
```
sha256:8d969eef6ecad3c29a3a629280e6cf00278f7d6e1f5f...
```

#### results.bag.mcap
- **内容**：完整 50Hz 仿真数据
- **Topic**：
  - `/own_ship_state`
  - `/targets/*`
  - `/scoring/*`
  - `/asdr/*`
  - `/l3/m{1-8}/*`
  - `/fault/*`
- **签名**：SHA256 签名

#### results.arrow
- **内容**：KPI 指标列存表
- **格式**：Apache Arrow IPC
- **列**：
  - timestamp
  - min_cpa_m
  - max_rot_dps
  - avg_sog_kn
  - safety_score
  - rule_score
  - delay_score
  - magnitude_score
  - phase_score
  - plausibility_score

#### asdr_events.jsonl
- **格式**：JSON Lines（每行一个事件）
- **用途**：人类可读的 ASDR 事件记录

**示例**：
```json
{"time": 222.1, "module": "M8", "type": "tor", "detail": "ToR → ROC, Reason: TDL < TMR"}
{"time": 315.0, "module": "M6", "type": "colreg", "detail": "Rule 14 activated, Target: 123456789"}
```

#### verdict.json
```json
{
  "run_id": "uuid",
  "verdict": "PASS",
  "overall_score": 0.91,
  "reason": "All COLREGs rules satisfied, min CPA >= 1.0nm",
  "kpis": {
    "min_cpa_nm": 0.8,
    "max_rot_dps": 3.2,
    "avg_sog_kn": 11.8
  },
  "colregs_chain": [
    {"rule": "R5", "status": "passed"},
    {"rule": "R7", "status": "passed"},
    {"rule": "R14", "status": "passed"},
    {"rule": "R17", "status": "passed"},
    {"rule": "R8", "status": "passed"}
  ],
  "scoring": {
    "safety": 0.92,
    "rule": 0.87,
    "delay": 0.95,
    "magnitude": 0.88,
    "phase": 0.90,
    "plausibility": 0.93,
    "overall": 0.91
  },
  "timestamp": "2026-05-13T10:10:00Z"
}
```

#### manifest.yaml
```yaml
version: "2.0"
run_id: "uuid"
scenario_id: "uuid"
scenario_hash: "sha256..."

generated_at: "2026-05-13T10:10:00Z"
author: "SIL Framework"

tools:
  sil_framework: "v2.0"
  l3_tdl_kernel: "v1.1.2"
  l3_tdl_kernel_git_sha: "abc123..."
  colav_simulator: "v1.0"
  maritime_schema: "0.2.1"

verification:
  dnv_rp_0513: true
  ccs_i_ship: true

signatures:
  scenario: "sha256:..."
  mcap: "sha256:..."
```

---

## 4. 6 维评分体系（Hagen 2022 / Woerner 2019）

### 4.1 评分维度

| 维度 | 英文名 | 权重 | 描述 |
|------|--------|------|------|
| **安全性** | Safety | 30% | 避让距离、最近会遇距离 |
| **规则合规** | Rule | 20% | COLREGs 规则满足程度 |
| **响应延迟** | Delay | 15% | 从检测到行动的时间 |
| **动作幅度** | Magnitude | 10% | 航向/航速变化程度 |
| **时机相位** | Phase | 15% | 行动时机是否合适 |
| **可信度** | Plausibility | 10% | 与人类驾驶员行为一致性 |

### 4.2 评分计算

**综合评分**：
```
Overall = 0.30 × Safety + 0.20 × Rule + 0.15 × Delay
        + 0.10 × Magnitude + 0.15 × Phase + 0.10 × Plausibility
```

**PASS/FAIL 判定**：
- ≥ 0.80 → **PASS**
- 0.50 - 0.80 → **MARGINAL**
- < 0.50 → **FAIL**

### 4.3 评分卡样式

```
┌─────────────────────────────────────────┐
│           6 维评分雷达图               │
│                                        │
│                 Safety                │
│                 0.92                   │
│                  │                     │
│    Delay ◯────────┼────────◯ Rule      │
│     0.95  │       │       │   0.87     │
│            │       │       │            │
│  Magnitude ●───────┼───────● Phase      │
│     0.88   │       │       │   0.90     │
│            │       │       │            │
│                 Plausibility            │
│                   0.93                 │
│                                        │
│         Overall: 0.91 ✅ PASS          │
└─────────────────────────────────────────┘
```

---

## 5. DNV-RP-0513 仿真器鉴定

### 5.1 鉴定文档模板

按照 DNV-RP-0513 要求，仿真器鉴定需包含：

| 章节 | 内容 |
|------|------|
| **目的与范围** | 鉴定目的、适用范围 |
| **模型描述** | 船舶模型、环境模型 |
| **校准数据** | 参考数据来源 |
| **验证结果** | 模型精度验证 |
| **不确定度分析** | 不确定度来源 |
| **局限性声明** | 模型限制条件 |

### 5.2 鉴定检查清单

| 检查项 | 说明 | 证据 |
|--------|------|------|
| 几何模型 | 船体几何参数准确性 | 船型图纸 |
| 水动力系数 | MMG 模型系数验证 | 试航数据 |
| 扰动模型 | 风/流模型校准 | 实测数据 |
| 积分精度 | RK4 vs 解析解误差 | 单元测试 |
| 时间同步 | UTC 同步精度 | NTP 日志 |
| 数据完整性 | MCAP 录制完整性 | 签名验证 |

---

## 6. CCS i-Ship 认证证据

### 6.1 证据要求

| 要求项 | 说明 | 证据 |
|--------|------|------|
| 功能完整性 | 避碰、防搁浅、航路回归 | 仿真场景覆盖 |
| COLREGs 合规 | 规则满足证明 | 6 维评分报告 |
| 系统可靠性 | MTBF/MTTR | 运行统计 |
| 安全性 | 安全功能验证 | 安全告警测试 |
| 人机接口 | 透明度要求 | ASDR 日志 |

### 6.2 证据清单

| 证据 | 文件 | 说明 |
|------|------|------|
| 场景定义 | scenario.yaml | maritime-schema 格式 |
| 仿真轨迹 | results.bag.mcap | 完整 50Hz 录制 |
| KPI 指标 | results.arrow | 数值指标 |
| ASDR 日志 | asdr_events.jsonl | 决策追溯 |
| 评分报告 | verdict.json | 6 维评分 |
| 版本清单 | manifest.yaml | 工具链版本 |
| 签名清单 | *.sha256 | 完整性验证 |

---

## 7. 导出流程

### 7.1 1-click 导出流程

```
用户点击"导出 Marzip"
    ↓
POST /api/v1/export/marzip?run_id={runId}
    ↓
FastAPI 后台任务启动
    ↓
┌────────────────────────────────────────────────────────────┐
│ 1. 读取 /var/sil/runs/{run_id}/bag.mcap                    │
│ 2. 计算 MCAP SHA256 → results.bag.mcap.sha256              │
│ 3. 提取 /scoring/* topics → polars DataFrame → Arrow IPC   │
│ 4. 计算 derived KPIs (min CPA, avg ROT, rule chain)        │
│ 5. 读取 scenario.yaml + 计算 SHA256 → scenario.sha256      │
│ 6. 生成 verdict.json (PASS/FAIL + 理由)                     │
│ 7. 生成 asdr_events.jsonl                                  │
│ 8. 生成 manifest.yaml                                       │
│ 9. 打包为 .marzip (zip 容器)                               │
│ 10. 保存到 /var/sil/runs/{run_id}/evidence.marzip         │
└────────────────────────────────────────────────────────────┘
    ↓
返回下载 URL
    ↓
前端显示"导出完成，可下载"
```

### 7.2 异步设计

- 后台任务运行，不阻塞 REST
- 支持轮询状态：`GET /export/status/{export_id}`
- 50Hz live 仿真期间也可后处理上一 run

---

## 8. 证据链验证

### 8.1 签名验证

```bash
# 验证场景签名
sha256sum scenario.yaml | awk '{print $1}' | diff - scenario.sha256

# 验证 MCAP 签名
sha256sum results.bag.mcap | awk '{print $1}' | diff - results.bag.mcap.sha256
```

### 8.2 完整性检查

```bash
# 解压 Marzip
unzip {run_id}_evidence.marzip -d extracted/

# 验证所有 SHA256
for f in scenario.yaml results.bag.mcap; do
  sha256sum "$f" | diff - "${f}.sha256" || exit 1
done

# 验证 verdict.json 格式
jq empty verdict.json || exit 1
```

---

## 9. 文档附录

### 9.1 maritime-schema 使用

场景定义使用 DNV maritime-schema v0.2.x 格式：

```yaml
# TrafficSituation 示例
type: TrafficSituation
version: "1.0"

environment:
  wind:
    speed_mps: 5.0
    direction_deg: 45.0
  current:
    speed_mps: 1.0
    direction_deg: 90.0
  visibility_nm: 5.0
  sea_state_beaufort: 2

participants:
  - id: own_ship
    vessel_type: FCB
    initial_state:
      lat: 63.430
      lon: 10.395
      heading_deg: 0.0
      speed_kn: 12.0
  - id: target_1
    vessel_type: CARGO
    initial_state:
      lat: 63.428
      lon: 10.400
      heading_deg: 180.0
      speed_kn: 10.0
    behavior: ais_replay
    mmsi: "123456789"

metadata:
  name: "Head-on R14"
  region: "trondheim"
  author: "SIL Framework"
  created_at: "2026-05-13T10:00:00Z"
```

### 9.2 版本控制

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-05-13 | 初始版本 |