# D2.2 实施状态追踪

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-D2.2-EVIDENCE-STATUS |
| 版本 | v0.1 |
| 日期 | 2026-05-22 |
| 状态 | 🟡 设计完成，代码实施待执行 |

---

## 交付物状态矩阵

| # | 交付物 | 轨道 | 状态 | 备注 |
|---|---|---|---|---|
| 1 | `cpa_tcpa_calculator.hpp/cpp` 扩展（UKF + 策略接口）| A | ⏳ 待实施 | 头文件枚举 + Config 字段扩展蓝图已在 plan.md §Task A1 |
| 2 | `TrackedTarget.msg` 扩展（5 新字段）| A/C | ⏳ 待实施 | cpa_covariance_m2 / tcpa_covariance_s2 / intent_confidence / brg_deg / rng_m |
| 3 | `world_state_aggregator.cpp` 置信度模型升级 | A | ⏳ 待实施 | cpa_quality_factor + track_age_factor 加权公式已设计 |
| 4 | `enc_loader.hpp/cpp` 扩展（多边形查询 + 混合触发）| B | ⏳ 待实施 | query_zone / in_tss / in_narrow_channel / refresh 接口已设计 |
| 5 | `world_model_node.cpp` 新增 15s 定时器 + L2 subscriber | B | ⏳ 待实施 | |
| 6 | `env_sanity_checker.hpp/cpp`（新建）| C | ⏳ 待实施 | 7 校验项已定义 |
| 7 | `m2_params.yaml` 扩展（cpa_covariance + enc_update 段）| A/B | ⏳ 待实施 | YAML 结构已在 spec §3.1.1 |
| 8 | `test_must1_overtaking_sector.cpp`（新建，6 测试）| D | ⏳ 待实施 | |
| 9 | `test_must6_sog_validation.cpp`（新建，4 测试）| D | ⏳ 待实施 | |
| 10 | `test_env_sanity_checker.cpp`（新建，7 测试）| D | ⏳ 待实施 | |
| 11 | `test_intent_confidence.cpp`（新建，5 测试）| D | ⏳ 待实施 | |
| 12 | `test_cpa_tcpa_calculator.cpp` 扩展（+4 UKF 测试）| A | ⏳ 待实施 | |
| 13 | `test_s57_parser.py` 重写（5 测试）| E | ⏳ 待实施 | |
| 14 | `test_geometry_serializer.py` 重写（4 测试）| E | ⏳ 待实施 | |
| 15 | `test_s102_processor.py`（新建，4 测试）| E | ⏳ 待实施 | |
| 16 | `test_ukc_calculator.py`（新建，5 测试）| E | ⏳ 待实施 | |
| 17 | `test_world_state_end_to_end.cpp`（新建，集成）| 集成 | ⏳ 待实施 | |
| 18 | `fm2_planner.py` deprecated 标记 | — | ⏳ 待实施 | |
| 19 | `D2.2-spec.md` | — | ✅ 完成 | v0.1，2026-05-21 |
| 20 | `D2.2-plan.md` | — | ✅ 完成 | 5 轨道 A-E + 集成，含代码蓝图 |

---

## 测试证据占位符

代码实施完成后，以下证据文件将填充到本目录：

| 文件名 | 内容 | 来源轨道 |
|---|---|---|
| `colcon-build-pass.txt` | `colcon build` 输出 | 集成 |
| `cpp-test-results.xml` | gtest XML 报告（≥34 测试）| A + D |
| `python-pytest-results.txt` | pytest 输出（18 测试）| E |
| `world-state-e2e-trace.json` | Mock 输入 → 输出消息字段验证日志 | 集成 |
| `enc-demo2-scenarios-pass.txt` | ≥5 场景 exclusion_zones 加载验证 | B + 集成 |

---

## 当前阻塞项

无外部阻塞。所有 5 轨道独立，可同时开工（per spec §2 方案 B 决策理由）。

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-05-22 | 初版，设计产物闭口后建立，实施状态均为待执行 |
