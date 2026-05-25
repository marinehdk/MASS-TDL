# D2.3 实装状态记录

| 属性 | 值 |
|---|---|
| 记录日期 | 2026-05-22 |
| 状态 | 实装完成，运行记录待归档 |

---

## 1. 核心源文件（已确认存在）

### 新增组件
| 文件 | 职责 |
|---|---|
| `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/current_error_monitor.hpp` | CEM 配置 / 结果类型 / 类声明 |
| `src/l3_tdl_kernel/m3_mission_manager/src/current_error_monitor.cpp` | CEM 实现（severity 判定 + L4 超时降级）|
| `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/l1_watchdog_monitor.hpp` | Watchdog 配置 / 结果类型 / 类声明 |
| `src/l3_tdl_kernel/m3_mission_manager/src/l1_watchdog_monitor.cpp` | Watchdog 实现（OK/WARNING/TIMEOUT 转移 + confidence_factor）|
| `src/l3_tdl_kernel/l3_external_msgs/msg/TrackingError.msg` | L4 XTE stub IDL（[TBD-L4TEAM] 字段待确认）|

### 修改文件
| 文件 | 变更摘要 |
|---|---|
| `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/mission_manager_node.hpp` | 新增 CEM + watchdog 成员 / 新方法 / NodeOptions 构造 |
| `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp` | 集成 CEM + watchdog 到 `on_tracking_error()` / `on_world_state()` / watchdog timer |
| `src/l3_tdl_kernel/m3_mission_manager/config/m3_params.yaml` | 新增 `current_error:` + `l1_watchdog:` 配置段 |

---

## 2. 测试文件（已确认存在）

### 单元测试
| 文件 | 覆盖范围 |
|---|---|
| `src/l3_tdl_kernel/m3_mission_manager/test/test_current_error_monitor.cpp` | spec §7.1 7 用例（双源正常 / XTE HIGH / 海流 HIGH / MEDIUM / L4 超时系列）|
| `src/l3_tdl_kernel/m3_mission_manager/test/test_l1_watchdog_monitor.cpp` | spec §7.2 5 用例（OK / WARNING边界 / TIMEOUT边界 / 恢复 / 多次恢复）|
| `src/l3_tdl_kernel/m3_mission_manager/test/test_idl_schema_version.cpp` | IDL schema_version=120 序列化/反序列化正确性 |

### 集成测试
| 文件 | 覆盖范围 |
|---|---|
| `src/l3_tdl_kernel/m3_mission_manager/test/integration/test_m3_dual_subscription.cpp` | IT-01～IT-06 双订阅独立性 + ODD-B 重规划链 + L4 current error 链 |
| `src/l3_tdl_kernel/m3_mission_manager/test/integration/test_m3_node_lifecycle.cpp` | 节点生命周期（已有，D2.3 回归）|

---

## 3. 待归档运行记录

以下文件**尚不存在**，colcon 测试通过后须补入本目录：

| 待建文件 | 来源 |
|---|---|
| `evidence/colcon-test.log` | `colcon test --packages-select m3_mission_manager` 标准输出 |
| `evidence/integration-test-results.txt` | IT-01～IT-06 + ODD-B 链 + L4 链全通输出 |

归档完成后 D2.3-report.md 升版为 v1.0。
