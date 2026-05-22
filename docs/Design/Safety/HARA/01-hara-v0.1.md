# L3 TDL 危险分析与风险评估 v0.1

| 属性 | 值 |
|---|---|
| 文档编号 | MASS-L3-TDL-HARA-001 |
| 版本 | v0.1（初始版本，参数待 HAZID RUN-001 2026-08-19 校准）|
| 日期 | 2026-06-16 |
| 架构基线 | v1.1.3-pre-stub |
| 标准 | IEC 61508-1 §7.3 + IEC 61508-1 Annex D + IMO MASS Code |
| 系统边界 | L3 TDL M1–M8 全模块（含模块间 DDS 接口）|
| 显式排外 | L3↔L4 接口 / L3↔Fusion 接口 / M7 FMEDA / ALARP → 推 D3.3/Phase 4 |
| 关闭 Finding | C P0-C-1(b)（HARA 缺失）；C P0-C-3 部分（SIF-01 独立性约束体现于缓解列）|
| Owner | 安全工程师外包（主）+ 架构师（review）|

## SIF 参考列表（§11.4 + D2.7 新增）

| SIF-ID | 功能 | 主责模块 | SIL | 状态 |
|---|---|---|---|---|
| SIF-01 | ODD 边界检测 + MRC 触发链 | M1 + M7 | SIL 2 | §11.4 直接引用 |
| SIF-02 | COLREGs 推理完整性 | M6 | SIL 2 | §11.4 直接引用 |
| SIF-03 | 轨迹安全校验（→ ENC）| M5 | SIL 1 | §11.4 直接引用 |
| SIF-04 | D4 通信链路监控 | M1 + M8 | SIL 2 | §11.4 直接引用 |
| SIF-05 | 世界模型数据完整性 | M2 | SIL 1 | D2.7 新增 |
| SIF-06 | 任务计划安全边界 | M3 | SIL 1 | D2.7 新增 |
| SIF-07 | 行为仲裁安全性 | M4 | SIL 1 | D2.7 新增 |
| SIF-08 | HMI 透明性完整性（SAT-1/2/3）| M8 | SIL 1 | D2.7 新增 |

## 风险图（IEC 61508-1 Annex D 海事适配）

严重度：C1=轻微偏航 / C2=CPA < 0.3nm / C3=碰撞或人员伤害 / C4=沉船或多人伤亡  
暴露频率：F1=年级（极端气象+稠密交通）/ F2=月级（港口/TSS/能见度<0.5nm）/ F3=周级（正常航行操纵）

| 严重度 ╲ 暴露 | F1 | F2 | F3 |
|---|---|---|---|
| C1 | SIL 0 | SIL 0 | SIL 1 |
| C2 | SIL 1 | SIL 1 | SIL 2 |
| C3 | SIL 1 | SIL 2 | SIL 2 |
| C4 | SIL 2 | SIL 2 | SIL 3 |

## HARA 危险源表

| Hazard-ID | Category | Module | Failure Description | Consequence | Severity | Exposure | SIL | SIF Reference | ODD-Subdomain | Health-State | Mitigation (Design) | Residual Risk | Notes |
| H-01 | CAT-A | M6 | COLREGs Rule 8/16/17 推理失败：避碰动作方向/幅度计算错误 | 与来船 CPA < DCPA_safe；碰撞风险 | C3 | F2 | SIL 2 | SIF-02 | ODD-B | DEG | M7 PERF 监控 + X-axis Checker VETO（ADR-001 §11.7）+ 独立 COLREGs 验证层 | Medium | [HAZID 校准] DCPA_safe 阈值；C P0-C-1(b) |
| H-02 | CAT-A | M5 | BC-MPC 输出航点穿越 ENC 安全等深线 | 船舶驶入浅水区或暗礁区；搁浅风险 | C3 | F2 | SIL 2 | SIF-03 | ODD-B | NOM | ENC 安全等深线约束 + M7 SIF-03 轨迹时效验证 | Medium | [HAZID 校准] 等深线余量参数 |
| H-03 | CAT-A | M1 | ODD 越界后 MRC 触发链失效：系统在 OUT-of-ODD 环境保持 D4 级运行 | 船舶无操作员监督持续自主运行；碰撞/搁浅风险升高 | C3 | F1 | SIL 1 | SIF-01 | ODD-D | CRIT | MRC 双轨触发（M1 + M7）+ D2/D3 降级路径（ADR-001 §11.6 MRM-01/02/03/04）| Low | ADR-001 缓解实装（D2.1）；独立性约束 C P0-C-3 部分 |
| H-04 | CAT-A | M7 | M7 Checker VETO 未送达 Doer：DDS liveliness 超时 | Doer 无安全监督持续生成危险轨迹；碰撞风险 | C3 | F2 | SIL 2 | SIF-01 | ALL | DEG | M1 M7 心跳超时（500ms lease_duration）→ DEGRADED + M8 红色告警 | Low | DDS liveliness QoS 已实装（D2.1 FM-11）|
| H-05 | CAT-A | M5 | Mid-MPC 优化发散：90s 时域内无可行轨迹 | 输出旧轨迹或零指令；船舶失控 | C4 | F1 | SIL 2 | SIF-03 | ODD-C | DEG | Fallback BC-MPC 备份路径 + M7 轨迹时效检查（< 2s）| Medium | [HAZID 校准] 优化发散触发频率 |
| H-06 | CAT-A | M2 | AIS/雷达目标融合失败：虚假目标或漏报目标 | 碰撞避险基于错误世界模型；避碰动作方向错误 | C3 | F2 | SIL 1 | SIF-05 | ODD-B | DEG | AIS + 雷达 + 视觉融合冗余；M7 数据一致性交叉检查 | Medium | SIF-05 D2.7 新增；[HAZID 校准] 融合误差阈值 |
| H-07 | CAT-A | M4 | IvP 行为仲裁返回物理不可达动作：ROT/速度超 Capability Manifest 限制 | 船舶尝试执行动力学限制外动作；碰撞风险 | C3 | F2 | SIL 2 | SIF-07 | ODD-B | NOM | IvP ROT/速度约束（Capability Manifest）+ M7 行为输出校验 | Medium | SIF-07 D2.7 新增 |
| H-08 | CAT-A | M3 | 任务计划越界：WP 超出 ENC 安全水域范围 | 规划航路导致船舶进入危险区；搁浅风险 | C2 | F2 | SIL 1 | SIF-06 | ODD-A | NOM | 航线 ENC 安全水域验证 + M7 边界检查 | Low | SIF-06 D2.7 新增 |
| H-09 | CAT-B | M4 | IvP 代价函数权重配置错误：避碰目标被低权重抑制 | 系统优先速度/航线而非避碰；COLREGs 违规 | C3 | F2 | SIL 2 | SIF-07 | ODD-B | NOM | Capability Manifest 权重约束 + M7 IvP 输出完整性校验 | Medium | SIF-07；[HAZID 校准] 权重阈值 |
| H-10 | CAT-B | M6 | Rule 17 主动避让权（Privileged Vessel）错误触发：本船主动避让当应保向 | 双方避碰动作混乱；险近加剧 | C3 | F2 | SIL 2 | SIF-02 | ODD-B | DEG | COLREGs 角色状态机一致性检查 + M7 SIF-02 推理审计 | Medium | [HAZID 校准] Rule 17 触发条件 |
| H-11 | CAT-B | M5 | BC-MPC 预测时域 < 90s：安全余量不满足 Veitch 2024 TMR 约束 | 轨迹安全余量不满足 DCPA；险近局面 | C3 | F3 | SIL 2 | SIF-03 | ODD-B | NOM | TMR ≥ 60s 架构约束（Veitch 2024）+ M1 TDL 联动预警 | Low | 架构 §11.4 SIF-03 直接引用 |
| H-12 | CAT-B | M3 | 重规划触发阈值过高：危险场景下未及时请求重规划 | 船舶持续执行次优计划；碰撞/险近风险累积 | C2 | F3 | SIL 1 | SIF-06 | ODD-B | DEG | M3 CPA 告警阈值 + M1 ODD 状态变化强制重规划 | Medium | [HAZID 校准] CPA 阈值 |
| H-13 | CAT-B | M1 | ODD 状态误判（NOMINAL → DEGRADED 漏触发）：系统以最优性能假设运行 | 降级场景采用正常性能阈值；安全余量不足 | C3 | F2 | SIL 2 | SIF-01 | ODD-C | NOM | ODD 状态机 Conformance Score 阈值（ADR-003）+ M7 交叉验证 | Medium | [HAZID 校准] Conformance Score 阈值 |
| H-14 | CAT-B | M6 | 碰撞场景几何分类错误（Head-On → Overtaking 误判）：动作指令方向错误 | 双方同向避让；险近加剧 | C3 | F2 | SIL 2 | SIF-02 | ODD-B | NOM | 相对运动几何预分类验证（M2 COLREG 预分类 + M6 复核）| Medium | [HAZID 校准] 方位角判断边界 |
| H-15 | CAT-B | M7 | M7 Checker 误 VETO 正确 Doer 决策（False Positive）：不必要 MRC 触发 | 不必要操作员接管；OOTB 场景；可用性下降 | C1 | F3 | SIL 0 | SIF-01 | ALL | NOM | Checker 阈值合理性定期校验 + DEMO-2 数据采集（误 VETO KPI）| Low | 安全失效（Safe Failure）；可用性指标监控 |
| H-16 | CAT-B | M6 | ODD-aware 参数切换错误：ODD-A→ODD-B 切换时 COLREGs 参数未同步 | 港口场景使用开阔水域避碰距离；安全余量不足 | C3 | F2 | SIL 2 | SIF-02 | ODD-B | NOM | M1→M6 ODD 状态同步协议 + 参数版本验证 | Medium | [HAZID 校准] 参数同步窗口 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
