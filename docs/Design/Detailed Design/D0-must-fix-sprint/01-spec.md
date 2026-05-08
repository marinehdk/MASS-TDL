# D0 · Pre-Kickoff Must-Fix Sprint — Spec

| 字段 | 值 |
|---|---|
| 文档 | D0 sprint 详细执行 spec |
| 版本 | v1.0 |
| 起草日 | 2026-05-08 |
| 锁定日 | 2026-05-08 EOD（5/8 brainstorming session 末态） |
| 适用窗口 | 2026-05-08（Fri）→ 2026-05-12（Tue），3 工作日 + 2 天 weekend buffer |
| Owner | 架构师-hat（统筹）；solo user 戴多角色 hat 并行执行（见 [user_solo_multi_role memory](file:///Users/marine/.claude/projects/-Users-marine-Code-MASS-L3-Tactical-Layer/memory/user_solo_multi_role.md)） |
| 上游依据 | v3.0 plan §2（[gantt:93-196](../../Architecture%20Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md)）；评审整合 §1+§13（[Review:29-46,273-289](../../Review/2026-05-07/00-consolidated-findings.md)） |
| 下游消费 | D1.1 ROS2 工作区 / D1.2 CI/CD / D1.3a MMG / D1.5 V&V Plan / D2.6 船长 ground truth / D2.7 HARA / D3.5 HAZID 132 [TBD] 回填 |
| 状态 | Draft → 5/8 EOD self-review → 5/12 EOD closure |

---

## §0 Scope & Out-of-Scope

### §0.1 In-Scope（本 sprint 必交付，否则 5/13 不应 Phase 1 D1.x 起跑）

1. **11 项 must-fix 关闭**：MUST-1..11（[Review:29-46](../../Review/2026-05-07/00-consolidated-findings.md)）
2. **6 项 multi-vessel cleanup**：MV-1/2/3/5/6/7（M2/M5/M8 中 FCB-only 字面量清除）
3. **3 份决策文档**：
   - `RFC-007-M7-X-axis-Heartbeat.md`（F P0-F-05；M7 ↔ Deterministic Checker 心跳契约）
   - `RFC-009-IvP-Implementation-Path.md`（B P0-B-01；libIvP LICENSE 复审 → 决策矩阵）
   - `RUN-001-fcb-data-substitute-memo.md`（A P0-A2；HAZID 6/10–6/24 vs gantt 12 月冲突解）
4. **MUST-11 工时表 v2.1**：M7 6→9pw 拆 M7-core/M7-sotif 工作内容文档（v3.0 §0 数学已自洽，仅显式标注 + signoff）
5. **D4.5 修订声明**（用户决策 §13.3）：12 月 FCB → 非认证级技术验证 + AIS 数据采集
6. **CI infra**：`.gitlab-ci.yml` 增 `multi_vessel_lint` + `path_s_dry_run` 两 job（warning mode）
7. **3 项 deep research 并行**（MUST-10）：safety_verification + ship_maneuvering + maritime_human_factors，5/8 AM 同时 dispatch
8. **CLAUDE.md §1.1 staleness fix**：将"100% 为 docs"措辞改为"32-D-task plan locked + D0 起代码与文档并行演进"
9. **Workspace bootstrap**：`feature/d0-must-fix-sprint` 分支 + 最小可运行 src/tests/config layout（pure-logic Python，零 ROS2 依赖；D1.1 wraps as ROS2 nodes）
10. **D0 closure-report.md + git tag v1.1.2-patch1**：5/12 EOD 自审签字 + 锚定状态

### §0.2 Out-of-Scope（明确不做）

| 项 | 不做的理由 | 落点 |
|---|---|---|
| ROS2 完整工作区 / IDL 全集 / colcon | D1.1（5/13–5/24）的工作；D0 不 commit ROS2 依赖 | D1.1 |
| CI/CD 5 阶段流水线 | D1.2（5/13–5/24）；D0 仅 2 个 lint job（warning mode） | D1.2 |
| 4-DOF MMG 仿真器 / AIS 历史数据管道 | D1.3a（5/13–5/31） | D1.3a |
| BC-MPC SLA 实测（B P0-B-04） | 5/27 W2 prototype 实测；本 sprint 仅记入 RFC-007/008 待办 | W2 |
| HAZID 议程修订 / 第①次会议本身 | 5/13 会议；D0 仅注入 talking-points | 5/13 |
| 4 缺失模块（ENC / ParamStore / EnvValidator / BNWAS）stub | D2.8（7/31）/ D3.8（8/31）/ B4 contingency D4.7 | Phase 2/3/4 |
| HTML 副本同步（v3.0 §D0.3 line 170,177） | 用户独立处理（5/8 brainstorming Q3 决策） | 用户外部 |
| 模块算法选型矩阵 / 仲裁优先级 | D2.8 v1.1.3 stub + D3.8 完整化 | Phase 2/3 |
| ALARP / SDLC 文档（B3 推迟 v1.1.4） | Phase 4 起 | Phase 4 |

### §0.3 审计追溯

每个 in-scope 项强制三联引用：(a) Finding ID（MUST-N / MV-N / 用户决策 §13.x） + (b) v3.0 plan 锚点 + (c) 评审 angle 报告 §X 起源。任务表 §3/§4/§5 各 task 行均含此三联。

---

## §1 Sprint 指导原则

继承 [全局 CLAUDE.md](file:///Users/marine/.claude/CLAUDE.md) Karpathy 四则；本 sprint 额外 6 条：

### §1.1 零幻觉证据链
- 每条声明指向 `file:line` 或 deep research 报告；libIvP LICENSE 必须 WebFetch 实证（不凭"我记得 GPL"）
- 置信度 🟢/🟡/🔴/⚫；deep research 报告内来源 ≥4 + A/B/C 分层
- 禁用语："我记得 / 一般 / 通常 / 应该 / 似乎"——出现一次 = self-review reject

### §1.2 多船型铁律（CLAUDE.md §4 决策四）
- 决策核心代码（`src/m{1..7}_*/`）零船型常量
- 字面量 `FCB / 45m / 18 kn / 22 kn / ROT_max=N / sog ∈ [0,30]` 出现 = `multi_vessel_lint` warning（5/12 EOD 0 hit）
- 船型差异通过 `Capability Manifest`（`config/vessels/*.yaml`）注入

### §1.3 Doer-Checker 独立性（CLAUDE.md §4 决策二 + IEC 61508 SIL 2）
- D0 不写 M7 代码，但 **PATH-S CI dry-run** 在 D0 落地，确保 5/13–7/20 期间 M1–M6 不引入跨 M7 边界依赖（MUST-8）
- M5 触发 MRM（FM-2）改 emit `safety_concern_event` 给 M7（MUST-9）；MRM 命令集仅由 M7 emit
- 单人多角色场景下：Doer 与 Checker 必须在**独立 Claude 会话**完成（不共享上下文）

### §1.4 Real-not-stub（[feedback memory](file:///Users/marine/.claude/projects/-Users-marine-Code-MASS-L3-Tactical-Layer/memory/feedback_no_fake_stubs.md)）
- 每个 must-fix 产出能跑/能测/能验的真实工件
- 禁止：`xfail` / `NotImplementedError` / 空 CI rule（"等有源码再启用"）
- 例外：模块依赖跨团队（如 L4 接口）或本 sprint 范围外（D1.x）的，文档化引用即可

### §1.5 角色独立性（[user_solo_multi_role memory](file:///Users/marine/.claude/projects/-Users-marine-Code-MASS-L3-Tactical-Layer/memory/user_solo_multi_role.md)）
- 6 项 surgical fix 的"PR review" = 用户戴另一角色 hat 在新 Claude 会话独立审
- M7 6→9pw 的"M7-owner agree" = M7-hat 自审 signoff
- libIvP RFC-009 的"法务复审" = 法务-hat 在独立会话基于 WebFetch 实证签字

### §1.6 Surgical Changes（Karpathy 第 3 条）
- 只改 finding 直接相关的 file:line + 必要的 cross-ref；禁止顺手统一格式 / 改其他章节措辞
- 发现新 finding：在 closure-report.md "新发现" 段落记录，不擅自处理

---

## §2 Workspace Bootstrap（5/8 Fri AM, ~1.5h）

### §2.1 任务

| ID | 工时 | 任务 |
|---|---|---|
| D0-BS-1 | 1h | `git checkout -b feature/d0-must-fix-sprint`（base = main HEAD = `5113558`）；建立 §2.2 layout 全部空文件/目录；首个 commit `chore(d0): bootstrap workspace skeleton` |
| D0-BS-2 | 1.5h | `pyproject.toml` + `.ruff.toml` + `pytest.ini` 配置；`pytest -v` 0 collected 0 failed；`ruff check src/ tests/` 0 violation；commit `chore(d0): toolchain config (python ≥3.11, pytest, pydantic v2, pyyaml, ruff)` |

### §2.2 Repository Layout

```
src/
  m2_world_model/
    __init__.py
    encounter_classifier.py     # MUST-1
    track_validator.py          # MUST-6
  m5_tactical_planner/
    __init__.py
    mpc_params.py               # MUST-2
    fallback_policy.py          # MUST-5 + MUST-9
  m8_hmi_bridge/
    __init__.py
    active_role.py              # MUST-7
  common/
    __init__.py
    capability_manifest.py      # MUST-6 (pydantic schema)
tests/
  m2/
    __init__.py
    test_encounter_classifier.py
    test_track_validator.py
  m5/
    __init__.py
    test_mpc_params.py
    test_fallback_policy.py
  m8/
    __init__.py
    test_active_role.py
  common/
    __init__.py
    test_capability_manifest.py
config/
  capability_manifest.schema.json    # 由 capability_manifest.py 导出
  vessels/
    fcb_45m.yaml                     # 基于 M5/M2 详设现有数据填充
.gitlab-ci.yml                       # multi_vessel_lint + path_s_dry_run
pyproject.toml
.ruff.toml
pytest.ini
docs/Design/Detailed Design/D0-must-fix-sprint/
  01-spec.md                         # 本文件
  02-coding-standards.md             # 多船型禁字 + PATH-S 边界
  99-closure-report.md               # 5/12 EOD 产出
docs/Design/Cross-Team Alignment/
  RFC-007-M7-X-axis-Heartbeat.md
  RFC-009-IvP-Implementation-Path.md
docs/Design/HAZID/
  RUN-001-fcb-data-substitute-memo.md
docs/Design/Detailed Design/M7-Safety-Supervisor/
  02-effort-split-v2.1.md            # MUST-11
```

### §2.3 Toolchain

| 工具 | 版本 | 用途 |
|---|---|---|
| Python | ≥3.11 | 纯逻辑模块 + 测试 |
| pydantic | ≥2.0 | Capability Manifest schema |
| pyyaml | ≥6.0 | vessel yaml 读取 |
| pytest | ≥8.0 | 单元测试 |
| ruff | ≥0.4 | lint + format |
| mypy | optional | 类型检查（warning 模式） |

**禁止**：本 sprint 不引入 ROS2 / rclpy / cmake / colcon / C++ 工具链（D1.1 范围）。

### §2.4 Bootstrap Acceptance

- [ ] feature 分支 push 到 origin
- [ ] `pytest` 输出 `0 passed, 0 failed`（空仓 baseline）
- [ ] `ruff check` 输出 `All checks passed`
- [ ] CI 在 push 时触发并产出 0 错（无 lint 规则尚未配置时跳过 lint job）

---

## §3 D0.1 — Surgical Fix + Multi-vessel Cleanup（1.0 pw = 39h）

每 task ≤4h；TDD 顺序在 §7 DAG 标注（test 先 → 实装 → 文档同步）。

### §3.1 MUST-1 · M2 OVERTAKING 扇区

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.1-T1.1 | 3h | M2 §5.1.3:387,417 文档修订 [225,337.5]→[112.5,247.5]；附 4 边界（112.5 / 180 / 247.5 / 边界±0.1°）解释 + v1.1.2-patch1 修订记录条目 | `docs/Design/Detailed Design/M2-World-Model/01-detailed-design.md` patch | diff 锚定 line 387,417；修订记录条目齐 |
| D0.1-T1.2 | 3h | `tests/m2/test_encounter_classifier.py`（TDD 先行）：4 边界 + 4 类型对称用例 = 8 用例 | test 文件，初次运行全 fail（无实装） | pytest 报 8 collected 8 failed |
| D0.1-T1.3 | 3h | `src/m2_world_model/encounter_classifier.py` 实装 `classify_encounter(own_hdg_deg, target_rel_brg_deg) → Encounter`；`Encounter = Enum{HEAD_ON, OVERTAKING, CROSSING_GIVE_WAY, CROSSING_STAND_ON, OUT_OF_ENCOUNTER}` | encounter_classifier.py ≤80 LOC | pytest 8 passed；ruff/mypy 0；零 ROS2/rclpy import |
| D0.1-T1.4 | 1h | 跨文档 spot-check：架构 §6.3.1:585 / M6 §5.1:311 引用一致；不一致一并改 | grep 报告 + 必要 patch | grep `\[225` 在 docs/ 0 hit；grep `\[112\.5` 命中 ≥3 文件 |

**小计**：10h；**关闭**：MUST-1（B P0-B-03）；**证据**：[Review:35](../../Review/2026-05-07/00-consolidated-findings.md), [M2:387,417](../M2-World-Model/01-detailed-design.md)

### §3.2 MUST-2 · Mid-MPC N=18 三处统一

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.1-T2.1 | 2h | `tests/m5/test_mpc_params.py`（TDD）：assert `MID_MPC_N == 18` / `MID_MPC_T_STEP == 5.0` / `MID_MPC_HORIZON_S == 90.0` / `N * T_step == HORIZON_S` | test 文件，初次 fail | 4 collected 4 failed |
| D0.1-T2.2 | 1h | `src/m5_tactical_planner/mpc_params.py` 实装：`from typing import Final`；3 个 `Final` 常量 | mpc_params.py ≤30 LOC | 4 passed；ruff 0 |
| D0.1-T2.3 | 3h | 文档同步 4 文件：架构 §10.3:850 / M5 §5.2:184 / M5 §7.2:272 / RFC-001 引用统一 N=18；各文件修订记录条目 | 4 份文档 patch | grep `N\s*=\s*1[02]\b` 在 4 文件 0 hit；grep `N\s*=\s*18` 命中 ≥4 处 |

**小计**：6h；**关闭**：MUST-2（B P0-B-02）；**证据**：[Review:36](../../Review/2026-05-07/00-consolidated-findings.md), [arch:850](../../Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md), [M5:184,272](../M5-Tactical-Planner/01-detailed-design.md)

### §3.3 MUST-5 · M5 FM-4 fallback `ROT_max=8°/s` 删除

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.1-T3.1 | 2h | `tests/m5/test_fallback_policy.py`（TDD，FM-4 部分）：(a) 进入 FM-4 时 emit `safety_concern_event` 而非 ROT 命令；(b) ROT 上限查询走 `manifest.rot_max_curve(u)`；(c) 无 `ROT_max=8` 字面量；3 用例 | test 文件 | 3 collected 3 failed |
| D0.1-T3.2 | 2h | `src/m5_tactical_planner/fallback_policy.py` 实装 FM-4 状态转移 `OUT_of_ODD → notify(M7) → wait MRM`；`rot_max_curve(u)` 接口签名（实装走 `manifest.rot_max_curve`） | fallback_policy.py FM-4 部分 ≤60 LOC | 3 passed；grep `ROT_max\s*=\s*\d` 在 src/ 0 hit |
| D0.1-T3.3 | 2h | M5 §7.2:674 文档修订 + arch §11.6:988-1007 fallback 矩阵 cross-ref + 修订记录 | M5 + arch patch | 修订记录齐；M5 §7.2 不再含 ROT 数值字面量 |

**小计**：6h；**关闭**：MUST-5 + MV-FCB-01（B FCB-01 / F P0-F-03）；**证据**：[M5:674](../M5-Tactical-Planner/01-detailed-design.md)

### §3.4 MUST-6 · M2 sog 校验 Capability Manifest 化

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.1-T4.1 | 3h | `tests/common/test_capability_manifest.py`（TDD）：(a) `max_speed_kn` 必填（缺 → ValidationError）；(b) `rot_max_curve` 单调；(c) `hull_class` Enum 校验；(d) JSON Schema export 与模型 round-trip；6 用例 | test 文件 | 6 collected 6 failed |
| D0.1-T4.2 | 3h | `src/common/capability_manifest.py` pydantic v2 模型实装：`max_speed_kn: float`（必填，>0）+ `rot_max_curve: list[tuple[float, float]]`（单调校验）+ `length_m: float` + `hull_class: HullClass` + JSON Schema 导出方法 | capability_manifest.py ≤120 LOC | 6 passed；ruff 0；schema export 文件 = `config/capability_manifest.schema.json` |
| D0.1-T4.3 | 3h | `tests/m2/test_track_validator.py`（TDD）：(a) sog ≤ max×1.2 → OK；(b) sog > max×1.2 → ValidationError；(c) 无 manifest 时 raise；4 用例 | test 文件 | 4 collected 4 failed |
| D0.1-T4.4 | 2h | `src/m2_world_model/track_validator.py` 实装 `validate_track(track, manifest) → ValidationResult`；零硬编 30/60/22 字面量 | track_validator.py ≤60 LOC | 4 passed；grep `\b30\s*kn\b\|\b60\s*kn\b` 在 src/m2 0 hit |
| D0.1-T4.5 | 2h | `config/vessels/fcb_45m.yaml` 填充：`max_speed_kn: 22.0` / `length_m: 45.0` / `hull_class: SEMI_PLANING` / `rot_max_curve: [[0, 8.0], [10, 6.0], [22, 3.0]]` ⚫（HAZID 校准前为占位） | fcb_45m.yaml | yaml lint 通过；schema 验证通过；⚫ 标注齐 |
| D0.1-T4.6 | 1h | M2 §2.2:48 文档修订 "sog ∈ [0,30] kn / FCB 满载极限" → "sog ≤ manifest.max_speed_kn × 1.2"；修订记录 | M2 patch | grep `\b30\s*kn\b\|FCB\s*满载` 在 M2 §2.2 0 hit |

**小计**：14h；**关闭**：MUST-6 + MV-2/3（F P0-F-03）；**证据**：[M2:48](../M2-World-Model/01-detailed-design.md)

### §3.5 MUST-7 · M8 active_role 双角色对称

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.1-T5.1 | 3h | `tests/m8/test_active_role.py`（TDD）：6 合法转移 + 4 非法拒绝 = 10 用例；初值取自 `manifest.primary_role`；DUAL_OBSERVATION 进出需双 ack | test 文件 | 10 collected 10 failed |
| D0.1-T5.2 | 3h | `src/m8_hmi_bridge/active_role.py` 实装 `Enum{PRIMARY_ON_BOARD, PRIMARY_ROC, DUAL_OBSERVATION}` + `ActiveRoleStateMachine` 类 | active_role.py ≤120 LOC | 10 passed；ruff 0；纯逻辑无 IO |
| D0.1-T5.3 | 2h | M8 §4.1:111 文档修订（去 `ROC_OPERATOR` 默认 → `manifest.primary_role`）+ RFC-004 ASDR 接口兼容性 spot-check + 修订记录 | M8 patch + RFC-004 review notes | 修订记录齐；RFC-004 兼容性 OK 或新 issue 单独 raise |

**小计**：8h；**关闭**：MUST-7 + MV-7（D P0-D-04）；**证据**：[M8:111](../M8-HMI-Transparency-Bridge/01-detailed-design.md)

### §3.6 MUST-9 · M5 FM-2 MRM-01 走 M7

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.1-T6.1 | 2h | `tests/m5/test_fallback_policy.py`（TDD，FM-2 部分增量）：(a) 进入 FM-2 时 emit `safety_concern_event{reason="FM-2 collision_imminent"}` 而非 `mrm_command_01`；2 用例 | test 文件增量 | 2 新用例 fail |
| D0.1-T6.2 | 2h | `src/m5_tactical_planner/fallback_policy.py` 实装 FM-2 路径变更 | fallback_policy.py FM-2 部分 ≤30 LOC | 2 用例 passed；grep `mrm_command\|MRM-01` 在 src/m5 0 hit |
| D0.1-T6.3 | 1h | M5 §7.2:672 文档修订 + arch §11.6:988-1007 cross-ref + Doer-Checker 论证段一句话提及 "M5 emits event, M7 emits MRM" | M5 + arch patch | 修订记录齐 |

**小计**：5h；**关闭**：MUST-9（B P1-B-08）；**证据**：[M5:672](../M5-Tactical-Planner/01-detailed-design.md), [arch:988-1007](../../Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md)

### §3.7 Multi-vessel Lint CI Job

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.1-T7.1 | 3h | `.gitlab-ci.yml` 增 `multi_vessel_lint` job：`grep -rE "(\bFCB\b\|\b45 ?m\b\|\b18 ?kn\b\|\b22 ?kn\b\|ROT_max\s*=\s*[0-9])" src/ tests/`；命中行写到 `multi_vessel_lint_report.txt`；warning mode（`allow_failure: true` + report artifact）；MR 自动 comment | .gitlab-ci.yml job ≤30 行 | feature 分支 push CI 触发；空 src/ 下 0 violation；故意加一行 `FCB` 触发后 warning 报告出现 |
| D0.1-T7.2 | 1h | `docs/Design/Detailed Design/D0-must-fix-sprint/02-coding-standards.md` 多船型禁字 list + 例外申报流程（CI 报告中的"why allowed"段填字） | 02-coding-standards.md ≤200 行 | 链接到 CI job + 例外申报模板 |

**小计**：4h；**关闭**：MV-cleanup baseline（决策四 enforcement）

**§3 D0.1 总计**：10 + 6 + 6 + 14 + 8 + 5 + 4 = **53h**

⚠️ 注：53h 超过 v3.0 plan 的 1.0 pw（40h）估计。原因：(B) 中粒度 ≤4h/task 把"PR review"工作显式化（每个 must-fix 末 1h 文档同步 + cross-ref spot-check 在 v3.0 估计中被合并到模块负责人 4h 工作里）。**调整方案**：跨 5/8 PM + 5/11 全天 + 5/12 AM 三天分布，允许 5/9–5/10 weekend buffer 吸收最后 5–8h；若 5/12 AM 仍未完成 §3，进入 §8 R0.1 触发逻辑。

### §3.8 Demo Charter D0.1

| 字段 | 内容 |
|---|---|
| Scenario | feature/d0-must-fix-sprint MR diff 现场（CI 全绿 + multi_vessel_lint 0 violation + ≥40 pytest 全绿） |
| Audience × View | 架构师-hat（结构验收）+ M2-hat / M5-hat / M8-hat 各自模块独立 review session |
| Visible Success | CI 截图 ≥40 passed / ruff 0 / multi_vessel_lint 0 violation；M2 OVERTAKING 8 边界用例 `pytest -v` 输出录像 |
| Showcase Bundle | feature 分支 commit 链表 + CI URL + 4 模块 diff（M2/M5/M8/common）+ encounter_classifier 调用示例 notebook |
| Contribution Map | MUST-1/2/5/6/7/9 + MV-1/2/3/5/6/7 → DEMO-1（6/15 Skeleton Live）"骨架已可加载真实算法包"基础 |

---

## §4 D0.2 — Decision / RFC Closure（0.5 pw = 18h）

### §4.1 RFC-007 · M7 ↔ X-axis Heartbeat

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.2-T8.1 | 2h | `/nlm-ask --notebook silhil_platform "DDS heartbeat patterns IEC 61508 watchdog timeout SIL 2"` + `/nlm-ask --notebook safety_verification "watchdog SIL 2 fail-silent fail-operational"` 整合输出 | research notes inline 在 RFC | 至少 4 来源 + 置信度 🟢/🟡 |
| D0.2-T8.2 | 4h | RFC-007 起草 7 章节：(1) 心跳周期候选（10Hz / 20Hz）+ 论证；(2) HeartbeatMsg payload schema（`stamp` + `seq` + `liveness_token` + `m7_internal_health`）+ DDS topic name + QoS（RELIABLE, KEEP_LAST(1)）；(3) timeout 三档（50ms warning / 200ms degraded / 500ms safety stop）；(4) failure semantics：超时如何升级到 X-axis VETO；(5) 跨团队验收清单（X-axis 团队 + Cyber 团队）；(6) 引用 IACS UR E26/E27 段；(7) 5/13 评审待答问题 list | RFC-007-M7-X-axis-Heartbeat.md ≤400 行 | 7 章节齐；引用 ≥3 sources；🟢/🟡 标注 |
| D0.2-T8.3 | 1h | `RFC-decisions.md` 增 RFC-007 条目（status=Pending 5/13 跨团队评审）+ M7 详设 stub cross-ref + arch §16(stub) 引用 | RFC-decisions patch | RFC-007 行齐 |

**小计**：7h；**关闭**：F P0-F-05；**证据**：[Review §3.C P1-C-5](../../Review/2026-05-07/00-consolidated-findings.md)

### §4.2 MUST-3 · RFC-009 IvP Implementation Path

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.2-T9.1 | 2h | WebFetch `https://github.com/moos-ivp/moos-ivp/blob/master/LICENSE` + `https://raw.githubusercontent.com/moos-ivp/moos-ivp/master/COPYING` 实证；并 `/nlm-research --depth fast "moos-ivp licensing GPL LGPL CCS commercial vessel software"` | research notes（含 LICENSE verbatim + commit hash） | 引用 commit hash + LICENSE 头 ≥10 行 verbatim；置信度 🟢 |
| D0.2-T9.2 | 4h | RFC-009 起草决策矩阵：3 候选 × 4 维 = 12 单元 + 推荐路径段 + 法务-hat sign-off 段（5/12 PM 填）；3 候选 = (a) 引入 libIvP（如 LICENSE 兼容） / (b) 自实现 minimal IvP "behavior-set + utility aggregator" / (c) 替代 RRT*-CBF；4 维 = license-compliance / effort weeks / IvP semantics 完整性 / Phase 1 onboard 风险 | RFC-009-IvP-Implementation-Path.md ≤500 行 | 12 单元齐备；推荐项标注 + 法务-hat 段 ⚪ 待签 |
| D0.2-T9.3 | 1h | RFC-decisions 注册 + M4 §5.6:805-806 引用 RFC-009 推荐项；如选自实现，M4 详设增 1.5pw 估计标注（B4 contingency 钩子） | RFC-decisions + M4 patch | 修订记录齐 |

**小计**：7h；**关闭**：MUST-3（B P0-B-01）；**证据**：[M4:805-806](../M4-Behavior-Arbiter/01-detailed-design.md)

### §4.3 MUST-4 · RUN-001 FCB 数据替代备忘录

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.2-T10.1 | 3h | HAZID 132 [TBD] 参数清单遍历（[01-odd-parameters.md](../../HAZID/01-odd-parameters.md) / 02 / 03 / 04 / 05）→ 每项标注 substitution method ∈ {水池实测, MMG sim, AIS 历史回放, 仅 sea-trial 可获取} + gap analysis 表 | substitution table ≥132 行 | 4 method 分布百分比给出；sea-trial-only 项 ≤ 15%（凭据：评审整合 §6 估计） |
| D0.2-T10.2 | 3h | memo 起草 6 段：(1) 冲突陈述（HAZID 6/10–6/24 vs gantt 12 月降级）；(2) 替代方案合理性（覆盖率 + 残差风险）；(3) 12 月 sea-trial 作补充而非校准证据；(4) CCS 不到场逻辑（隔离非认证试航与认证档案）；(5) HAZID 议程影响（≥2 个 full-day workshop + CCS 持续介入）；(6) CCS-hat 自审签字段（5/12 PM 填） | RUN-001-fcb-data-substitute-memo.md ≤400 行 | 6 段齐 + 引用 §4.3-T10.1 表 |
| D0.2-T10.3 | 1h | HAZID kickoff 5/13 议程注入：增 memo 宣读 5min + Q&A 15min；更新 [`RUN-001-kickoff.md`](../../HAZID/RUN-001-kickoff.md) 议程 timeline | RUN-001-kickoff.md patch | 议程 timeline 重排；总时长不超 8h |

**小计**：7h；**关闭**：MUST-4（A P0-A2）+ 用户决策 §13.3 部分支撑

### §4.4 Demo Charter D0.2

| 字段 | 内容 |
|---|---|
| Scenario | 5/13 HAZID kickoff 第①次会议现场（user 戴 HAZID 主持-hat），架构师-hat 宣读 RFC-007 + RFC-009 + memo 三件套 |
| Audience × View | HAZID 干系人各自独立审：设计-hat 看 RFC-007 contract / V&V-hat 看 RFC-009 effort 估计 / HF-hat 看 memo 中 ToR 覆盖隐含 / CCS-hat 看 memo 中 12 月降级合规 |
| Visible Success | CCS-hat 在 memo signoff 段填字 ✅；M4-hat 在 RFC-009 sign-off 段选定方案；safety-hat 在 RFC-007 timeout 三档段填字 |
| Showcase Bundle | 3 份文档 PDF 导出 + RFC-decisions.md diff + HAZID kickoff 议程更新 diff |
| Contribution Map | MUST-3/4 + F P0-F-05 → 5/13 HAZID 起跑无 license/数据/接口阻塞；用户决策 §13.3 落地 |

---

## §5 D0.3 — Effort + Deep Research + CI + Doc Drift（0.5 pw = 17.5h）

### §5.1 MUST-10 · 3 项 Deep Research 并行（5/8 AM 启动）

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.3-T11.1 | 0.5h | 5/8 AM 09:00 同时 dispatch 3 subagent（每个 ~15min wall-clock）：<br>**(a) safety_verification**：`/nlm-research --depth deep --add-sources "DNV-RP-0671 ABS Autonomous Vessel Guide IMO MSC.110 MASS L3 SIF requirements"` <br>**(b) ship_maneuvering**：`/nlm-research --depth deep --add-sources "FCB-class 45m crew boat semi-planing 18-25kn 4-DOF MMG validation hydrodynamic coefficient hull"` <br>**(c) maritime_human_factors**：`/nlm-research --depth deep --add-sources "Veitch 2024 ToR takeover scenario adaptive dataset BNWAS IMO MSC.282(86) operator state"` | 3 subagent 启动追踪 ID | 3 subagent 启动；每个 ~15min 后回报 |
| D0.3-T11.2 | 3h | 5/8 PM 收 3 报告 → 每份 ≤500 字摘要 + 关键 finding 入对应下游 D-task 备忘：<br>safety → D1.8 ConOps + D2.7 HARA<br>ship → D2.8 §10.5 4-DOF 适用性边界声明<br>HF → D2.6 船长 ground truth + D P0-D-02 ToR 矩阵 | 3 摘要 + integration map | 每报告 ≥4 来源；置信度 🟢/🟡 ；3 notebook source 计数 +N（截图） |

**小计**：3.5h；**关闭**：MUST-10

### §5.2 MUST-11 · M7 6→9pw 拆分文档

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.3-T12.1 | 3h | `docs/Design/Detailed Design/M7-Safety-Supervisor/02-effort-split-v2.1.md`：<br>**M7-core (6w)** = (a) Doer-Checker 仲裁逻辑 / (b) IEC 61508 SIL 2 baseline 实装 / (c) 20+ 失效模式 FMEDA 表 / (d) watchdog + heartbeat client 实装<br>**M7-sotif (3w)** = (a) ISO 21448 6 类映射 / (b) SOTIF triggering condition catalog / (c) RFC-007 集成 / (d) 与 D2.7 HARA 闭环 | 02-effort-split-v2.1.md ≤300 行 | 2 子项 deliverable 清单齐；周分布 candidate（候选 8 周窗口）列出；M7-hat 自审签字段 |
| D0.3-T12.2 | 1h | v3.0 plan §0.1 line 47 "+3.0" 锚点 acknowledge 注释；附录 A 工时表 v2.1 戳记（数学未变，仅 explicit acknowledge）；M7-hat 自审 signoff（独立会话） | v3.0 plan patch（minimal）+ M7-hat signoff log | line 47 注释含 "v2.1 锚定 5/12"；附录 A 头标注 v2.1 |

**小计**：4h；**关闭**：MUST-11 + 用户决策 §13.2

### §5.3 D4.5 修订声明（用户决策 §13.3）

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.3-T13.1 | 2h | 起草 D4.5 修订声明 5 段：(1) 12 月 FCB → 非认证级技术验证 + AIS 数据采集；(2) CCS 不到场理由 + 风险隔离论证；(3) 实船门槛 7→4 项明细（无 AIP / 无 SIL 2 第三方意见 / D3.7 8h 无崩溃 + D4.2 HIL ≥50h + Hs ≤ ODD-A 边界 + ROC 接管链路独立验证 ≥60s）；(4) 认证级试航延 2027 Q1/Q2 AIP 受理后；(5) 影响 D5.x 计划注脚 | 修订声明段（计入 v3.0 plan §6 + 附录 B） | 5 段齐；CCS-hat 自审签字段 ⚪ 待 5/12 PM 填 |
| D0.3-T13.2 | 1h | v3.0 plan §1（月度地图）+ §6 Phase 4 + 附录 B 里程碑表 三处修订一致；CLAUDE.md §1.2 路线进度 D4.5 行同步 | v3.0 + CLAUDE.md patch | grep "12 月.*认证级" 0 hit；grep "12 月.*非认证" ≥3 hit |

**小计**：3h；**关闭**：用户决策 §13.3

### §5.4 MUST-8 · PATH-S CI Dry-run

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.3-T14.1 | 3h | `.gitlab-ci.yml` 增 `path_s_dry_run` job：(a) `grep -rE "from\s+m7_safety_supervisor\b\|^import\s+m7_safety_supervisor\b\|#include.*m7-checker" src/m1_*/ src/m2_*/ src/m3_*/ src/m4_*/ src/m5_*/ src/m6_*/`；(b) `allow_failure: true`（warning mode）；(c) 命中行写 `path_s_report.txt` 作 artifact；(d) MR 自动 comment（GitLab API） | .gitlab-ci.yml job ≤40 行 | feature 分支 push 触发；空 src/ 下 0 hit |
| D0.3-T14.2 | 1h | 故意在 `src/m2_world_model/encounter_classifier.py` 末加 `# from m7_safety_supervisor import x`（注释形式可被 grep 抓） + push 触发 → 验证 warning 报告 + MR comment 链路 → revert | CI run log 截图 + revert commit | warning 出现 + comment 出现 + revert 后 0 hit |

**小计**：4h；**关闭**：MUST-8

### §5.5 CLAUDE.md §1.1 Staleness Fix

| ID | 工时 | 任务 | 关键产出 | Acceptance |
|---|---|---|---|---|
| D0.3-T15.1 | 2h | CLAUDE.md §1.1 修订："仍 100% 为 docs / 无源代码、无构建、无测试" → "v3.0 plan locked 2026-05-08；D0 起源代码与文档并行演进；32-D-task plan 是判定基准"；§1.2 路线进度增 "D0 ⏳ 5/8–5/12（Workspace bootstrap + 11 must-fix）"；§10 阅读入口加 D0 spec | CLAUDE.md patch | grep "100% 为 docs\|100% 为 \`docs/\`" 0 hit；§10 含 D0 spec 链接 |

**小计**：1h；**关闭**：CLAUDE.md drift（自发现，5/8 brainstorming）

**§5 D0.3 总计**：3.5 + 4 + 3 + 4 + 1 = **15.5h** ≈ 0.39 pw（v3.0 估 0.5 pw，富余 0.11 pw 吸收 deep research synthesis 不确定性）

### §5.6 Demo Charter D0.3

| 字段 | 内容 |
|---|---|
| Scenario | 5/12 PM 17:00 EOD self-review session（user 戴 PM-hat + 业主-hat + CCS-hat 三轮独立会话） |
| Audience × View | PM-hat 看工时表 v2.1 + 资源日历；业主-hat 看 D4.5 降级声明 + closure-report；CCS-hat 看 RFC + memo + D4.5 合规边界 |
| Visible Success | closure-report.md 11 finding 全勾 ✅ + 3 deep research notebook source 计数变化截图 + CI 全绿（multi_vessel_lint + path_s_dry_run 双 0 hit）+ git tag v1.1.2-patch1 已打 |
| Showcase Bundle | closure-report.md + CLAUDE.md diff + v3.0 plan §0/§1/§6 diff + 3 deep research 摘要 PDF + git tag URL |
| Contribution Map | MUST-8/10/11 + 用户决策 §13.2/13.3 落地 → 5/13 HAZID kickoff + Phase 1 D1.x 起跑零悬挂 finding |

---

## §6 Owner-by-Day 矩阵

> 角色 hat = solo user 在不同 Claude 会话戴的"角色帽"（[user_solo_multi_role memory](file:///Users/marine/.claude/projects/-Users-marine-Code-MASS-L3-Tactical-Layer/memory/user_solo_multi_role.md)）。"并行" 表示该时段 user 可以同时跑多个 Claude 会话（每个 hat 独立上下文）。

### §6.1 5/8 Fri（D0 Day 1，可用 ~7h）

| 时段 | 主 hat | 任务 ID | 备注 |
|---|---|---|---|
| 09:00–09:30 | 架构师-hat | D0-BS-1（workspace bootstrap）+ D0.3-T11.1（dispatch 3 deep research subagent，并行后台跑） | 后台 deep research ~15min wall-clock |
| 09:30–11:00 | 架构师-hat | D0-BS-2（toolchain config） | deep research subagent 后台运行 |
| 11:00–12:00 | 法务-hat（独立会话） | D0.2-T9.1（libIvP LICENSE WebFetch + nlm-research fast） | deep research subagent 已收到 |
| 12:00–13:00 | — | 午休（buffer） | |
| 13:00–14:00 | 架构师-hat | D0.2-T8.1（NLM RFC-007 research） | |
| 14:00–17:00 | 架构师-hat | D0.3-T11.2（synthesize 3 deep research → 摘要 + 集成图） | 3h |
| 17:00–18:00 | 架构师-hat | D0.3-T15.1（CLAUDE.md §1.1 fix） | 1h；EOD push commit |

**5/8 EOD 检查**：feature 分支 push；3 deep research 摘要齐；libIvP LICENSE 实证录入；CLAUDE.md staleness fix commit。

### §6.2 5/9 Sat / 5/10 Sun

**Strict buffer，不安排任务**（[5/8 brainstorming Q1 决策](../../Review/2026-05-07/00-consolidated-findings.md)）。如 5/8 滑期可用于追平；正常情况下休息。

### §6.3 5/11 Mon（D0 Day 2，可用 ~8h）

代码密集日。多 hat 并行三窗口（M2-hat / M5-hat / M8-hat），每个 hat 独立会话。

| 时段 | 主 hat A | 主 hat B | 主 hat C |
|---|---|---|---|
| 09:00–12:00 | M2-hat：D0.1-T1.1, T1.2, T1.3, T1.4（MUST-1 全套） | M5-hat：D0.1-T2.1, T2.2, T2.3（MUST-2 全套） | M8-hat：D0.1-T5.1, T5.2, T5.3（MUST-7 全套） |
| 12:00–13:00 | 午休 | 午休 | 午休 |
| 13:00–17:00 | M2-hat：D0.1-T4.1, T4.2, T4.3, T4.4, T4.5, T4.6（MUST-6 全套） | M5-hat：D0.1-T3.1, T3.2, T3.3（MUST-5）+ D0.1-T6.1, T6.2, T6.3（MUST-9） | 架构师-hat：D0.1-T7.1, T7.2（multi-vessel lint CI）+ D0.2-T8.2（RFC-007 起草 4h） |
| 17:00–18:00 | 架构师-hat（独立 review session）：M2/M5/M8 PR review；3 模块独立 spot-check | | |

**5/11 EOD 检查**：6 项 must-fix 代码 + 文档 全部 commit；CI ≥40 pytest 全绿；multi_vessel_lint 0 hit；RFC-007 草稿就绪。

### §6.4 5/12 Tue（D0 Day 3，可用 ~8h，含 closure session）

| 时段 | 主 hat | 任务 |
|---|---|---|
| 09:00–11:00 | 法务-hat（独立会话） | D0.2-T9.2（RFC-009 决策矩阵起草 + 法务-hat sign-off 段填字） |
| 09:00–11:00 | 架构师-hat（并行） | D0.2-T9.3（RFC-decisions 注册 + M4 patch）→ 等 9.2 完成 |
| 11:00–12:00 | M7-hat（独立会话） | D0.3-T12.1 part 1（M7-core / M7-sotif 工作内容拆分） |
| 12:00–13:00 | 午休 | |
| 13:00–15:00 | M7-hat | D0.3-T12.1 part 2 完 + D0.3-T12.2（v3.0 plan v2.1 注释） |
| 13:00–15:00 | safety-hat（并行） | D0.2-T10.1（HAZID 132 [TBD] substitution table） |
| 15:00–16:00 | safety-hat | D0.2-T10.2（memo 起草） |
| 15:00–16:00 | infra-hat（并行） | D0.3-T14.1（PATH-S CI job 配置） |
| 16:00–17:00 | safety-hat | D0.2-T10.3（HAZID 议程更新）+ D0.2-T8.3（RFC-007 RFC-decisions 注册） |
| 16:00–17:00 | infra-hat | D0.3-T14.2（PATH-S CI dry-run 验证）+ D0.3-T13.1（D4.5 修订声明）+ D0.3-T13.2（plan + CLAUDE.md sync） |
| **17:00–19:00** | **closure session** | **D0 closure：见 §9** |

**5/12 EOD 检查**：见 §9 closure criteria。

### §6.5 时段并行约束

- 同一 hat 不能两个会话并行（context 冲突）；不同 hat 可并行
- "PR review" sessions 与 "实装" sessions 必须不同 hat（角色独立性 §1.5）
- 3 deep research subagent 是后台 dispatch，不占 hat 时段

---

## §7 Dependency DAG

```
D0-BS-1 (workspace bootstrap)
   ├──> D0-BS-2 (toolchain)
   │      ├──> 所有 src/tests 任务（D0.1-T1..T7）
   │      ├──> CI 任务（D0.1-T7.1, D0.3-T14.1）
   │      └──> 文档任务可不依赖 toolchain，仅依赖 BS-1（分支存在）
   │
   ├──> 所有 docs/Design/ patch 任务
   │
   └──> D0.3-T11.1 (dispatch 3 deep research，并行后台)
          └──> D0.3-T11.2 (synthesize) — 5/8 PM
                 ├──> D1.8 / D2.7 / D2.6 / D2.8 备忘（不在本 sprint 关闭）
                 └──> 影响 D0.3-T12.1（safety_verification 报告作 M7 split 输入）

MUST-1 / 2 / 5 / 6 / 7 / 9（D0.1）TDD 强制顺序：
  test 先 (Tn.1) → impl (Tn.2) → 文档 (Tn.3/Tn.4)
  允许 test + 文档同 hat 同会话连续；impl 在 test fail 后启动

MUST-6 (D0.1-T4.x) 内部依赖：
  T4.1 (manifest test) → T4.2 (manifest impl) → T4.3 (track test) → T4.4 (track impl)
                                                              └──> T4.5 (yaml fixture) → 所有 track impl 测试
  T4.6 (M2 §2.2 patch) 仅依赖 BS-1（可与代码并行）

MUST-3 (D0.2-T9.x) 强依赖：
  T9.1 (LICENSE 实证) → T9.2 (决策矩阵) → T9.3 (注册 + M4 patch)

MUST-4 (D0.2-T10.x) 强依赖：
  T10.1 (substitution table) → T10.2 (memo) → T10.3 (HAZID 议程)

MUST-10 (D0.3-T11.x) 5/8 关键路径：
  T11.1 (dispatch 09:00) ──~15min──> 报告返回 ──> T11.2 (synthesize)

MUST-11 (D0.3-T12.x) 软依赖：
  T11.2 (safety_verification 报告) ──> T12.1 (M7-core/sotif 拆分)（不强制，但报告可做 input 增强）

MUST-8 (D0.3-T14.x) 强依赖：
  D0-BS-2 (CI baseline) → T14.1 (PATH-S job) → T14.2 (验证)

CLAUDE.md fix (D0.3-T15.1) 软依赖：
  D0.3-T13.2 (D4.5 plan sync) 同 patch 区域 → 建议同 commit / 同会话

closure (§9) 强依赖：
  全部 §3+§4+§5 任务完成 → §9 closure-report → git tag
```

### §7.1 关键路径

5/8 早 09:00 BS-1 → 5/8 晚 17:00 CLAUDE.md fix（中间 deep research synthesize）  
→ 5/11 全天 6 项 must-fix 代码 + RFC-007 草稿  
→ 5/12 早 RFC-009 决策 + memo + M7 split + CI + D4.5  
→ 5/12 17:00 closure session（2h，含 self-review + git tag）

**关键路径长度**：5/8 7h + 5/11 8h + 5/12 8h = 23h 直线时间；并行后等价工作 ≈ 74h（§3+§4+§5 合计），靠 5/11 三 hat 并行 + 5/12 多 hat 并行 + 5/8 deep research 后台并行实现压缩。

### §7.2 TDD 顺序硬约束

所有 D0.1 代码任务：先写测试（fail expected）→ 再写实装（让测试 pass）→ 最后文档同步。executing-plans 阶段强制按此序。违反 = self-review reject。

---

## §8 Risk & Contingency

### §8.1 已识别风险（继承 v3.0 §2 + 5/8 brainstorming 新增）

| # | 风险 | 概率 × 影响 | 缓解 / Contingency |
|---|---|---|---|
| **R0.1** | 5/12 内 RFC-009 LICENSE 法务-hat 不能下结论（如 LICENSE 文本歧义） | 中 × 高 | RFC-009 备选方案"先按自实现起手，libIvP 推到 Phase 2 末作可选优化"；5/12 EOD 即使 sign-off 段未填，标 ⚪ "Pending until 5/14" 不阻塞 closure |
| **R0.2** | 5/8 onboard V&V 工程师-hat 节奏滑（即用户当日精力不足跑 V&V hat） | 高 × 中 | V&V hat 在 D0 仅参与 D0.3-T11.2 synthesize 的 V&V 视角审核（≤1h）；如滑期延 5/11；不阻塞 D0 关键路径 |
| **R0.3** | HAZID-FCB memo CCS-hat 自审拒签（如逻辑漏洞被 CCS-hat 抓出） | 中 × 中 | 备选 plan：HAZID workshop ≥ 2 个 full-day（5/27 + 6/24）已在 v3.0 §1 注脚；memo 仅声明"workshop 量级补偿"即可；5/12 EOD 标 ⚠️ "需 5/13 现场修订" |
| **R0.4 (新)** | §3 D0.1 实际 53h 超 v3.0 估 40h 13h | 中 × 中 | (a) 5/9–5/10 weekend 吸收 5–8h 溢出；(b) MUST-9 (5h) 工作量低，可压到 3.5h；(c) 文档 cross-ref spot-check (T1.4/T2.3 等) 合并为单次 grep 全仓扫描节 1.5h |
| **R0.5 (新)** | 5/11 三 hat 并行实际并发限制（user 单人能否真"同时"跑 3 会话） | 中 × 中 | 3 hat 并行实际是"切片"——每 1h 切换一个 hat；TDD 测试-impl 之间天然有等待空隙可切换；估算 8h 工作日实际能 cover 18–22h 工作量 |
| **R0.6 (新)** | deep research subagent 5/8 AM 失败（NLM session 过期 / 网络） | 低 × 中 | 备选：5/8 PM 改用 fast research 单独跑，每个 0.5h；牺牲深度但完成度可达 |
| **R0.7 (新)** | feature 分支 5/12 EOD merge 回 main 时 conflict（main 在 D0 期间被其他 commit 推进） | 低 × 低 | user solo 模式下 main 不会在 D0 期间被 commit；如需，rebase 回主线 |
| **R0.8 (新)** | PATH-S CI job 误报（T14.2 故意触发后忘 revert） | 低 × 低 | T14.2 明确含 revert 步骤；CI 全绿门槛检查含 path_s_dry_run 0 hit |

### §8.2 升级触发器

任一条命中 → user 立即在主会话提出，进入决策点：

- 5/11 18:00 § 3 D0.1 完成率 < 70% → 触发 R0.4 缓解 (a)+(b)+(c)
- 5/12 14:00 § 4 D0.2 完成率 < 50% → 触发 R0.1 备选 + R0.3 备选
- 5/12 17:00 closure session 启动时 11 finding 关闭 < 9 → 进入"延期 24h"决策（5/13 上午补齐再 git tag；HAZID kickoff 议程顺延半天）
- 任何 R0 命中 → closure-report.md "风险触发" 段记录

### §8.3 严禁路径（继承 v3.0 §0.3）

- (a) 牺牲 SIL 场景质量降通过率门槛
- (b) 跳过 V&V Plan
- (c) 让 D-Charter 必填项空缺
- (d) **本 sprint 新增**：跳过 TDD 顺序（先 impl 后 test）
- (e) **本 sprint 新增**：commit `xfail` / `NotImplementedError` 占位（违反 §1.4 real-not-stub）
- (f) **本 sprint 新增**：在 src/ 加任何 ROS2 import（违反 §2.3）

命中 = 项目 P0 escalation。

---

## §9 Closure Criteria & Closure-Report 模板

### §9.1 D0 关闭强制判据（5/12 EOD）

下列每条 ✅ = D0 关闭，可触发 5/13 Phase 1 D1.x + HAZID kickoff：

#### §9.1.1 工件存在性

- [ ] feature/d0-must-fix-sprint 分支已 push
- [ ] `src/m2_world_model/{encounter_classifier.py, track_validator.py}` 存在
- [ ] `src/m5_tactical_planner/{mpc_params.py, fallback_policy.py}` 存在
- [ ] `src/m8_hmi_bridge/active_role.py` 存在
- [ ] `src/common/capability_manifest.py` 存在
- [ ] `tests/{m2,m5,m8,common}/test_*.py` 全部存在
- [ ] `config/capability_manifest.schema.json` + `config/vessels/fcb_45m.yaml` 存在
- [ ] `.gitlab-ci.yml` 含 `multi_vessel_lint` + `path_s_dry_run` 两 job
- [ ] `pyproject.toml` + `.ruff.toml` + `pytest.ini` 存在
- [ ] `docs/Design/Detailed Design/D0-must-fix-sprint/{01-spec.md, 02-coding-standards.md, 99-closure-report.md}` 存在
- [ ] `docs/Design/Cross-Team Alignment/{RFC-007-..., RFC-009-...}` 存在
- [ ] `docs/Design/HAZID/RUN-001-fcb-data-substitute-memo.md` 存在
- [ ] `docs/Design/Detailed Design/M7-Safety-Supervisor/02-effort-split-v2.1.md` 存在

#### §9.1.2 行为正确性（CI 全绿）

- [ ] `pytest tests/ -v` ≥ 40 passed 0 failed
- [ ] `ruff check src/ tests/` 0 violation
- [ ] CI `multi_vessel_lint` job 0 hit（warning 报告空）
- [ ] CI `path_s_dry_run` job 0 hit（warning 报告空）
- [ ] grep 全 src/ 字面量净化：`grep -rE "(\bFCB\b|45 ?m|18 ?kn|22 ?kn|ROT_max\s*=\s*\d|sog.*\b30\b)" src/` 0 hit

#### §9.1.3 Finding 关闭

| Finding | 证据锚点 | 状态 |
|---|---|---|
| MUST-1 | encounter_classifier.py + 8 pytest + M2 §5.1.3 patch | [ ] |
| MUST-2 | mpc_params.py + 4 pytest + arch §10.3 + M5 §5.2/§7.2 + RFC-001 patch | [ ] |
| MUST-3 | RFC-009 decisions matrix + 法务-hat signoff | [ ] |
| MUST-4 | RUN-001-fcb-data-substitute-memo.md + CCS-hat signoff + HAZID 议程 patch | [ ] |
| MUST-5 | fallback_policy.py FM-4 + 3 pytest + M5 §7.2 patch | [ ] |
| MUST-6 | capability_manifest.py + track_validator.py + 10 pytest + M2 §2.2 patch + fcb_45m.yaml | [ ] |
| MUST-7 | active_role.py + 10 pytest + M8 §4.1 patch | [ ] |
| MUST-8 | path_s_dry_run CI job + T14.2 验证 log | [ ] |
| MUST-9 | fallback_policy.py FM-2 + 2 pytest + M5 §7.2 patch + arch §11.6 cross-ref | [ ] |
| MUST-10 | 3 deep research 摘要 + notebook source 计数变化截图 | [ ] |
| MUST-11 | 02-effort-split-v2.1.md + M7-hat signoff + v3.0 plan v2.1 注释 | [ ] |
| MV-1/2/3/5/6/7 | multi_vessel_lint CI 0 hit | [ ] |
| 用户决策 §13.2 | MUST-11 关闭 + v3.0 §0.1 line 47 v2.1 acknowledge | [ ] |
| 用户决策 §13.3 | D4.5 修订声明 + v3.0 §1 + §6 + 附录 B + CLAUDE.md §1.2 sync | [ ] |
| CLAUDE.md drift（自发现） | CLAUDE.md §1.1 + §10 patch | [ ] |

#### §9.1.4 角色 signoff（独立会话）

- [ ] 架构师-hat：99-closure-report.md 全文 review
- [ ] M2-hat：MUST-1 + MUST-6 工件 review
- [ ] M5-hat：MUST-2 + MUST-5 + MUST-9 工件 review
- [ ] M8-hat：MUST-7 + active_role 状态机 review
- [ ] M4-hat：RFC-009 决策接受
- [ ] M7-hat：02-effort-split-v2.1.md 接受
- [ ] 法务-hat：RFC-009 sign-off 段填字
- [ ] CCS-hat：RUN-001 memo signoff 段填字 + D4.5 修订声明 review
- [ ] PM-hat：工时表 v2.1 acknowledge + 资源日历更新
- [ ] V&V-hat：3 deep research synthesize 视角审

#### §9.1.5 Git 锚定

- [ ] 5/12 EOD `git tag v1.1.2-patch1`（base = feature 分支 HEAD）
- [ ] feature 分支 merge 回 main（fast-forward 或 squash-merge，user 选）
- [ ] tag push 到 origin
- [ ] CHANGELOG-style 总结 commit message

#### §9.1.6 下游解锁

- [ ] 5/13 09:00 HAZID kickoff 议程已注入 RFC-007 + RFC-009 + memo 三 talking-points（D0.2-T10.3）
- [ ] 5/13 09:00 Phase 1 D1.1 ROS2 工作区可基于 v1.1.2-patch1 tag checkout（src/ pure-logic 模块作为 ROS2 节点 wrap 起点）

### §9.2 Closure-Report 模板

文件：`docs/Design/Detailed Design/D0-must-fix-sprint/99-closure-report.md`

```markdown
# D0 Closure Report

| 字段 | 值 |
|---|---|
| 起期 | 2026-05-08 09:00 |
| 终期 | 2026-05-12 19:00 |
| 总工时 | __h（实际）vs 估 74.5h |
| 关闭日期 | 2026-05-12 |
| 关闭判定 | ✅ 全闭 / ⚠️ 部分关闭 / ❌ 延期 |

## §1 工件清单（§9.1.1）
[逐项勾选]

## §2 行为正确性（§9.1.2）
[CI 截图 + grep 报告粘贴]

## §3 Finding 关闭表（§9.1.3）
[16 行关闭表 + 证据链接]

## §4 角色 signoff（§9.1.4）
[10 个 hat signoff 时间戳]

## §5 Git 锚定（§9.1.5）
- v1.1.2-patch1 tag SHA: ____
- feature 分支 merge commit: ____
- main 当前 HEAD: ____

## §6 下游解锁状态（§9.1.6）

## §7 风险触发记录
[R0.x 是否触发；触发时缓解动作；遗留风险]

## §8 deep research 关键 finding 入下游 D-task 备忘
- safety_verification → D1.8 / D2.7 备忘：____
- ship_maneuvering → D2.8 §10.5 备忘：____
- maritime_human_factors → D2.6 / ToR 矩阵备忘：____

## §9 新发现 finding（D0 期间发现，超 11 must-fix 范围）
[逐项 + owner + 关闭日期估计]

## §10 Lessons Learned（≤300 字）
[user 自评：估算偏差 / 角色切换效率 / TDD 顺序执行情况 / 工具链摩擦]

签字：架构师-hat ____ / PM-hat ____ / 业主-hat ____
```

---

## §10 Downstream Interface（D0 outputs → 下游 D-task 消费）

| D0 产出 | 消费 D-task | 消费方式 | 时点 |
|---|---|---|---|
| feature 分支 + v1.1.2-patch1 tag + src/m{2,5,8} pure-logic 模块 | D1.1 ROS2 工作区 | 作为 ament_python 包基础；wrap 为 ROS2 节点；保持 pure-logic 与 ROS2 adapter 分层 | 5/13–5/24 |
| .gitlab-ci.yml `multi_vessel_lint` + `path_s_dry_run` 两 job | D1.2 CI/CD 5 阶段流水线 | 作为 stage 1（lint + 静态检查）的子 job；warning mode 升级为 blocking 由 D1.2 完整化决定 | 5/13–5/24 |
| `config/capability_manifest.schema.json` + `fcb_45m.yaml` | D1.3a 4-DOF MMG + AIS 历史数据管道 | MMG 仿真器初始化时读 capability_manifest；hull_class=SEMI_PLANING 触发 6-DOF 预留逻辑 | 5/13–5/31 |
| `pyproject.toml` + pytest baseline | D1.4 编码规范 + 静态分析工具链 | 在 pyproject.toml 增 mypy strict / coverage / additional ruff rules；不重置 baseline | 5/25–5/31 |
| 3 deep research 摘要 → safety_verification | D1.8 ConOps v0.1 + cert-evidence-tracking + D2.7 HARA + FMEDA M1 | 直接引用作 [Rxx] 文献 | 6/8–7/27 |
| 3 deep research 摘要 → ship_maneuvering | D2.8 v1.1.3 §10.5 4-DOF 边界声明 stub + B P0-B-05 整改 | 直接引用作 [Rxx] 文献；FCB 18-25kn 高速段保真度残差测试任务 | 7/31 stub / 8/31 完整 |
| 3 deep research 摘要 → maritime_human_factors | D2.6 船长 ground truth + D P0-D-02 ToR 矩阵 + D3.5' 培训课程 | 直接引用作 [Rxx] 文献；BNWAS IMO MSC.282(86) 接入 M8 | 6/16–7/27 / 8/31 |
| RFC-007 M7↔X-axis Heartbeat | D2.7 HARA M7 部分 + D3.3a Doer-Checker 三量化矩阵 + D3.9 RFC-007 cyber 落地 | RFC-007 草稿作 D3.9 起跑点；X-axis 团队 5/13 评审 | 6/16–8/31 |
| RFC-009 IvP Implementation Path | D2.1 M4 Behavior Arbiter 实装 | 推荐方案决定 M4 编码起点；如选自实现，M4 工作量 +1.5pw（B4 contingency） | 6/16–7/31 |
| RUN-001-fcb-data-substitute-memo | D3.5 HAZID 132 [TBD] 回填 + RUN-001 8/19 完成 | substitution table 作 HAZID workshop input；CCS 持续介入凭据 | 5/13–8/19 |
| 02-effort-split-v2.1.md | D3.7 M7 实装 | M7-core 6w + M7-sotif 3w 拆分作 7/13–8/31 资源日历输入 | 7/13–8/31 |
| D4.5 修订声明 | D4.5 12 月 FCB 试航执行 + D5.x 2027 Q1/Q2 认证级试航 | 试航准入门槛 7→4 项；CCS 不到场；AIS 数据采集为认证级补充 | 12 月 / 2027 Q1/Q2 |
| CLAUDE.md §1.1 fix | 所有未来 Claude 会话起点 | 立即生效（user 全局 CLAUDE.md 不变；项目 CLAUDE.md 头部更新） | 5/13 起 |

### §10.1 接口契约 invariants

- pure-logic 模块（src/m{2,5,8}_*）保持零 ROS2 依赖到 D1.1 wrap 时；D1.1 wrap 必须 import-only，不修改 pure-logic 内部
- Capability Manifest schema 在 D1.x 期间只允许向后兼容扩展（加字段，不改类型）；破坏性改动需 RFC
- multi_vessel_lint 规则集在 D1.2 stage 1 中升级为 blocking mode；规则正则不变（除非新船型加入）
- path_s_dry_run 规则集在 D2.7 M7 实装启动时（7/20）升级为 blocking mode；M7 边界跨入即 CI 红
- RFC-007 5/13 评审后版本号升 v0.2 / v1.0；本 sprint 产 v0.1 草稿
- RFC-009 5/12 法务-hat sign-off 后冻结；M4 实装基于冻结版

---

## §11 Revision History

| 版本 | 日期 | 变更 | 编辑 hat |
|---|---|---|---|
| v0.1 | 2026-05-08 | 初稿（brainstorming session 末态） | 架构师-hat（user） |
| v1.0 | 2026-05-08 | 锁定，5/8 09:00 起执行 | 架构师-hat（user） |
| _ | _ | _ | _ |

---

## §12 文档约束

- 本 spec 自身在 D0 sprint 期间为 immutable 引用（一旦 v1.0 锁定不再修订）
- 5/12 EOD self-review 中如发现 spec 缺陷，记录在 closure-report.md §10 Lessons Learned；不就地修 spec
- spec 真正的"修订"留到 D0 → D0.5（如有）/ D0-retrospective 文档；本 sprint 不开启
- 后续 D-task 引用本 spec 时锚定 git commit hash（5/8 EOD 锁定时记录）

---

*spec 完毕 · 2026-05-08 · brainstorming session 出 · 总长 ~1100 行 · 锁定 5/8 EOD · D0 closure 5/12 EOD*
