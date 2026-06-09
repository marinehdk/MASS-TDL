# COLREGs 快速探针场景套件

机读场景，schema `scenarios/fcb_traffic_situation.schema.json`（v3.0）。加载器
`tools/sil/scenario_spec.py`；编排器 `src/sil_orchestrator`（`ScenarioStore` 递归扫描）。

## 定位（三套测试集分工）

| 集合 | 定位 | 维护 |
|---|---|---|
| **本目录 `COLREGs测试/`** | **快速 dev 探针集** —— 近距起步（~2 NM，total_time ~4-7 min）、**单一目的**、单船、可手调。改 bug 时的"听诊器"，定位快。 | 由 `gen_colreg_tier12.py` **生成（唯一真源）**，勿手改 YAML |
| `IMAZU标准测试/` | **冻结验收基准**（Imazu-22，Sawada/Tengesdal & Johansen 2023） | 几何冻结 + sha256 哈希保护，**勿动** |
| `ais_derived/` | 真实 AIS 航迹 demo / 集成 | 后续复杂场景 |

> 多船 give-way×stand-on 冲突属 Imazu-22 的活，**本探针集只放单船单规则**，保持失败可归因。

## 探针清单（8 个，单一目的）

| scenario_id | COLREGs | OS 角色 | 期望动作 | cpa_min | 测什么 |
|---|---|---|---|---|---|
| `colreg-rule14-ho` | R14 | give-way | 右转 | 926 | 纯正遇（M6 onset-latch 回归基线） |
| `colreg-rule14-ho-port` | R14 | give-way | **右转** | 926 | 目标偏左 5°，仍须右转（"对遇绝不左转/不当作穿越"） |
| `colreg-rule13-ot` | R13 | give-way | 右转 | 926 | 追越；Rule 13(d) 方位前移**不得重分类**（行为断言见 Phase B） |
| `colreg-rule15-cs` | R15/R16 | give-way | 右转 | 926 | 右舷穿越让路 |
| `colreg-rule15-cs-2` | R15/R16 | give-way | 右转 | 926 | 右舷穿越**短-TCPA**，逼早动作（Rule 8(b)） |
| `colreg-rule15-cs-edge` | R15 | give-way | 右转 | 500\* | **边界**：正遇/穿越交界（rel_brg 25°） |
| `colreg-rule15-ot-boundary` | R15 | give-way | 右转 | 500\* | **边界**：穿越/追越交界（rel_brg 108° ≈ 112.5° 线） |
| `colreg-rule17-cr-so` | R17/R15 | **stand-on** | 保向→末段 17(b) | 500\* | 左舷目标应让不让（直线 replay）→ 触发 R17(b)；测**本船不提前避让** |

\* 边界几何下定点机动达不到满船域、stand-on 末刻 17(b) 清不出满船域 → **500m 诚实下限**（非 0、非低于安全距，符合 Rule 8；A4000 真实 MPC 应超过）。其余 give-way 探针 = 926m（0.5 NM 船域）。

## 设计约束（"能反映真问题"）

- **近距起步 + DCPA≈0**：`solve_collision_target` 求目标航速使直线 DCPA≈0（纯正遇/追越用 `straight_target` 直接放置，避免求解退化）。无动作必碰 → 系统不避就红，杜绝假绿。
- **有效判据**：cpa_min 全部非 0、绑定船域（旧 `cs-3` 的 `cpa_min=0` 已删——CPA≥0 永真，无效）。
- **边界覆盖**：正遇/穿越（`ho-port` ↔ `cs-edge` 夹击）+ 穿越/追越（`ot-boundary`）两条扇区边界——bug 都住边界（M6 fishtail 就住正遇 ±6° 边界）。

## 本地校验（无需 A4000）

```bash
python -m tools.sil.gen_colreg_tier12      # 重生成 8 个（clean-regen：自动清残留）
python -m tools.sil.verify_colreg_tier12   # schema + loader + 真实 M2 分类 + DCPA<500
python tools/validate_scenarios.py --all   # 全仓 schema
pytest tools/sil/test_simulate.py          # kinematic 自洽（ho 可赢 ≥cpa_min）
```

## 打分层（A4000 验收用，复用现有）

`src/sim_workbench/sil_nodes/scoring/`：
- `rule_compliance_evaluator.py` — R13/14/15/16/17 → full/partial/violated（查 `rudder_side`/`heading_change_deg`/`role`/`timing_stage`）。R14 左舵=violated；R17 直航船早期大转向=violated。
- `kpi_deriver.py` — `min_cpa_nm` / `max_rudder_deg` / `avg_rot_dpm`（操纵平滑度）/ `grounding_risk_score`。

> **Phase B 待加**：行为稳定性断言（conflict 不翻转 / 航向单调→锁定无 fishtail / M5 plan 不 VALID↔EMPTY 翻 / 分类 onset 固定）—— 唯一能逮 fishtail·flap 类行为 bug（纯 CPA 判据逮不到，M6 fishtail 就是例证）。

## Tier-3 极限场景（暂缓，需 harness 改动）

1. **不合作机动目标**（对遇中目标违规左转切入）：`target_vessel_node` 仅支持直线 `replay` 与随机游走 `ncdm`，**无脚本化机动**。需新增脚本化机动目标模式或接入 `trajectory_file` 重放。
2. **受限地缘/geofence 交叉**：schema 无任意多边形 geofence 字段，静态危险仅来自 ENC（`enc_path`）。
3. **受限能见度（Rule 19）**：无 give-way/stand-on，双方都减速；需起雾 + 盲化光学传感器路径。

> 副作用利用：因目标恒为直线，"应让而不让"的直航测试（R17 末段 17(b)）无需改 harness 即成立 —— 已用于 `rule17-cr-so`。
