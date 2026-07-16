# P0: manifest 几何修正 + Nomoto 字段语义澄清 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修正 M5 vessel manifest 几何参数与实际 FCB 船型的偏差(28m/95t→45m/145t),澄清 Nomoto 字段语义(重命名 `nomoto_K_inv_s`→`nomoto_K_s` 存 K 本身),为 DP-02 Nomoto 预测模型奠基。

**Architecture:** 纯配置 + 局部消费代码 + 测试 fixture 的 behavior-preserving 修正。6 文件成对更新(yaml key + loader key + hpp 字段 + nomoto_fallback 成员 + fixture + header 默认值)。运行时无行为变化(经消费者链探索验证:mass/length 唯一消费者 VDM 无生产 caller;T_s 活路径 r₀=0 消失;K 纯存储)。

**Tech Stack:** C++17, ROS2 ament_cmake, yaml-cpp, gtest, colcon

**Spec:** `docs/superpowers/specs/2026-07-16-m5-p0-manifest-nomoto-fix-design.md`

## Global Constraints

- 工作目录: `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`(任务分支 codex/m5-design-grounding)
- 字段重命名必须**成对更新 6 文件**:yaml key + loader key + hpp 字段 + nomoto_fallback 成员 + fixture + header 默认值。loader 用 `yaml_get` 默认值回退,**漏改 key 会静默加载默认值不报错**,必须用断言"解析值==yaml 值"捕获。
- P0 **不触碰** VDM 删除/propagate_own/Nomoto 接入 NLP(推 P2)。
- 几何/Nomoto 新值: length 45.0 / beam 8.0 / draft 1.55 / mass 145000.0 / T_s 6.0 / K_s 0.3(均标 [TBD-HAZID] 海试校准)。
- 测试用 `m5_add_gtest` 宏 + `M5_TEST_FIXTURE_DIR` compile definition;fixture 路径 `test/fixtures/fcb_capability_fixture.yaml`。
- 每个 task 结束 commit;TDD(先写失败测试,再实现)。

---

## File Structure

| 文件 | 责任 | P0 改动 |
|---|---|---|
| `src/l3_tdl_kernel/m5_tactical_planner/config/fcb_vessel_capability.yaml` | 生产 manifest 配置 | 几何修正 + Nomoto 字段重命名/重估 |
| `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/shared/capability_manifest.hpp` | CapabilityManifest struct 定义 + 默认值 | 字段重命名 + 几何默认值更新 |
| `src/l3_tdl_kernel/m5_tactical_planner/src/shared/capability_manifest.cpp` | yaml loader | loader key 跟随 `K_inv_s`→`K_s` |
| `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/nomoto_fallback.cpp` | NomotoFallback 成员初始化 | 成员名跟随 |
| `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/nomoto_fallback.hpp` | NomotoFallback 成员声明 + 注释 | 成员名 + 注释跟随 |
| `src/l3_tdl_kernel/m5_tactical_planner/test/fixtures/fcb_capability_fixture.yaml` | 测试 fixture(精确断言用) | 镜像新值 |
| `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_vessel_dynamics_model.cpp` | VDM + manifest 解析测试 | 新增几何/Nomoto 解析断言 + 消费者回归 |
| `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt` | 测试注册 | (若新测试文件则注册;本 plan 复用现有 test_vessel_dynamics_model,无需改 CMake) |

---

## Task 1: Nomoto 字段重命名(struct + loader + nomoto_fallback)

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/shared/capability_manifest.hpp` (字段 `nomoto_K_inv_s` L86 + 注释 L84)
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/shared/capability_manifest.cpp` (loader key `"K_inv_s"`)
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/nomoto_fallback.hpp` (成员 `nomoto_K_inv_s_` L67 + 注释)
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/nomoto_fallback.cpp` (成员初始化 L18)
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_vessel_dynamics_model.cpp` (现有 ManifestFieldsParsed 测试附近)

**Interfaces:**
- Produces: `CapabilityManifest::Config::nomoto_K_s` (double, 替代 `nomoto_K_inv_s`); `NomotoFallback::nomoto_K_s_` (成员)

- [ ] **Step 1: 先写失败测试(字段重命名后旧名应编译失败)**

在 `test/unit/test_vessel_dynamics_model.cpp` 的 `ManifestFieldsParsed` 测试(L335 附近)内,把对旧字段名的引用改为新名,并加 K_s 值断言。先读该测试当前内容确认结构,然后在 `EXPECT_NEAR(cfg.rot_max_at_18kn_rad_s, ...)` 附近加:

```cpp
  // P0: Nomoto 字段重命名 K_inv_s -> K_s(存 K 本身),值 0.3;T_s 重估 6.0
  EXPECT_NEAR(cfg.nomoto_K_s, 0.3, 1.0e-9)
      << "nomoto_K_s must load K itself (renamed from K_inv_s)";
  EXPECT_NEAR(cfg.nomoto_T_s, 6.0, 1.0e-9)
      << "nomoto_T_s must be re-estimated to 6.0 (was 15.0)";
```

- [ ] **Step 2: 运行测试确认失败(字段未重命名 → 编译错误)**

Run: `colcon build --packages-select l3_tdl_kernel --cmake-args -DM5_BUILD_TESTS=ON 2>&1 | grep -E "nomoto_K_s|error" | head`
Expected: 编译错误 `'nomoto_K_s' was not declared` (字段还是旧名 `nomoto_K_inv_s`)

- [ ] **Step 3: 重命名 struct 字段(capability_manifest.hpp L84-86)**

读 `include/m5_tactical_planner/shared/capability_manifest.hpp` L84-86,把:
```cpp
  // Nomoto 1st-order model (DEMO-2 fallback, [TBD-HAZID]).
  double nomoto_T_s{15.0};
  double nomoto_K_inv_s{0.08};
```
改为:
```cpp
  // Nomoto 1st-order model T·ṙ + r = K·δ ([TBD-HAZID] sea-trial calibration).
  double nomoto_T_s{6.0};     // yaw time constant [s] (was 15.0, [R22] scaled estimate mid)
  double nomoto_K_s{0.3};     // rudder gain K [1/s] (renamed from K_inv_s; stores K itself)
```

- [ ] **Step 4: 重命名 loader key(capability_manifest.cpp)**

读 `src/shared/capability_manifest.cpp` nomoto section loader(搜索 `K_inv_s`),把:
```cpp
  yaml_get<double>(nm, "K_inv_s", cfg.nomoto_K_inv_s);
```
改为:
```cpp
  yaml_get<double>(nm, "K_s", cfg.nomoto_K_s);
```

- [ ] **Step 5: 重命名 NomotoFallback 成员(nomoto_fallback.hpp + .cpp)**

读 `include/m5_tactical_planner/mid_mpc/nomoto_fallback.hpp` L67 附近,把成员声明 + 注释:
```cpp
  double nomoto_K_inv_s_;  // Nomoto rudder gain [1/s] (stored for future use)
```
改为:
```cpp
  double nomoto_K_s_;  // Nomoto rudder gain K [1/s], Tṙ+r=Kδ (stored for future use)
```
读 `src/mid_mpc/nomoto_fallback.cpp` L18,把成员初始化:
```cpp
  nomoto_K_inv_s_(manifest.config().nomoto_K_inv_s)
```
改为:
```cpp
  nomoto_K_s_(manifest.config().nomoto_K_s)
```

- [ ] **Step 6: 更新 fixture nomoto section(test/fixtures/fcb_capability_fixture.yaml)**

把 fixture 的 nomoto block:
```yaml
  nomoto:
    T_s: 15.0
    K_inv_s: 0.08
```
改为:
```yaml
  nomoto:
    T_s: 6.0
    K_s: 0.3
```

- [ ] **Step 7: 更新生产 yaml nomoto section(config/fcb_vessel_capability.yaml)**

读生产 yaml 的 nomoto block,改为:
```yaml
  nomoto:
    # Nomoto 1st-order Tṙ+r=Kδ; values are [R22] scaled estimates, [TBD-HAZID] sea-trial
    T_s: 6.0
    K_s: 0.3
```

- [ ] **Step 8: 构建测试二进制 + 运行,确认新断言通过**

Run: `colcon build --packages-select l3_tdl_kernel --cmake-args -DM5_BUILD_TESTS=ON && colcon test --packages-select l3_tdl_kernel --event-handlers console_direct+ 2>&1 | grep -E "test_vessel_dynamics_model|PASSED|FAILED|nomoto_K"`
Expected: test_vessel_dynamics_model PASSED(新断言 nomoto_K_s==0.3 / nomoto_T_s==6.0 通过;旧字段引用已消除,编译成功)

- [ ] **Step 9: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/shared/capability_manifest.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/src/shared/capability_manifest.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/nomoto_fallback.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/nomoto_fallback.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/fixtures/fcb_capability_fixture.yaml \
        src/l3_tdl_kernel/m5_tactical_planner/config/fcb_vessel_capability.yaml \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_vessel_dynamics_model.cpp
git commit -m "refactor(m5): rename nomoto_K_inv_s -> nomoto_K_s (stores K itself), re-estimate T_s 15->6.0

P0 of M5 MPC redesign (DP-02/TBD-5). Field name K_inv_s contradicted header
comment 'rudder gain [1/s]' and was never used in K-vs-1/K arithmetic
(NomotoFallback delta=0). Rename to nomoto_K_s storing K itself per Nomoto
model Tr+r=Kd. T_s re-estimated 15->6.0 ([R22] scaled-estimate mid, range 2-10s).
Values are [TBD-HAZID] pending sea trial. Behavior-preserving (no live consumer)."
```

---

## Task 2: 几何参数修正(length/beam/draft/mass)

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/shared/capability_manifest.hpp` (geometry 默认值 L39-45)
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/config/fcb_vessel_capability.yaml` (geometry section)
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/test/fixtures/fcb_capability_fixture.yaml` (geometry section)
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_vessel_dynamics_model.cpp`

**Interfaces:**
- Produces: 更新后的 `CapabilityManifest::Config` geometry 默认值(length_m 45.0 / beam_m 8.0 / draft_m 1.55 / mass_kg 145000.0)

- [ ] **Step 1: 先写失败测试(几何断言)**

在 `test/unit/test_vessel_dynamics_model.cpp` 的 `ManifestFieldsParsed` 测试内,geometry 断言:
```cpp
  // P0: 几何参数修正为实际 FCB(45m LOA / 8.0m beam / 1.55m draft / ~145t)
  EXPECT_NEAR(cfg.length_m, 45.0, 1.0e-9);
  EXPECT_NEAR(cfg.beam_m, 8.0, 1.0e-9);
  EXPECT_NEAR(cfg.draft_m, 1.55, 1.0e-9);
  EXPECT_NEAR(cfg.mass_kg, 145000.0, 1.0e-9);
```

- [ ] **Step 2: 运行测试确认失败(旧几何值)**

Run: `colcon test --packages-select l3_tdl_kernel --event-handlers console_direct+ 2>&1 | grep -E "length_m|mass_kg|FAILED" | head`
Expected: FAIL(length_m 期望 45.0 实际 28.0;mass_kg 期望 145000 实际 95000)

- [ ] **Step 3: 更新 hpp geometry 默认值(capability_manifest.hpp L39-45)**

读 L39-45,把:
```cpp
    double length_m{28.0};
    double beam_m{6.5};
    double draft_m{1.4};
    // ...
    double mass_kg{95000.0};  // [TBD-HAZID] from inclining experiment
```
改为:
```cpp
    double length_m{45.0};    // LOA (LBP 44.1); [R22] FCB datasheet
    double beam_m{8.0};       // [R22] FCB datasheet
    double draft_m{1.55};     // [R22] build spec
    // ...
    double mass_kg{145000.0};  // displacement mid (130-160T range); [TBD-HAZID] inclining
```

- [ ] **Step 4: 更新生产 yaml geometry section**

读 `config/fcb_vessel_capability.yaml` geometry block,改为:
```yaml
  geometry:
    length_m: 45.0    # LOA (LBP 44.1); [R22] FCB datasheet, was 28.0
    beam_m: 8.0       # [R22] FCB datasheet, was 6.5
    draft_m: 1.55     # [R22] build spec, was 1.4
    mass_kg: 145000.0 # displacement mid (130-160T); [TBD-HAZID] inclining, was 95000
```

- [ ] **Step 5: 更新 fixture geometry section**

把 fixture geometry block:
```yaml
  geometry:
    length_m: 28.0
    beam_m: 6.5
    draft_m: 1.4
    mass_kg: 95000.0
```
改为:
```yaml
  geometry:
    length_m: 45.0
    beam_m: 8.0
    draft_m: 1.55
    mass_kg: 145000.0
```

- [ ] **Step 6: 构建并运行 test_vessel_dynamics_model,确认几何断言通过 + VDM 物理回归**

Run: `colcon build --packages-select l3_tdl_kernel --cmake-args -DM5_BUILD_TESTS=ON && colcon test --packages-select l3_tdl_kernel --event-handlers console_direct+ 2>&1 | grep -E "test_vessel_dynamics_model|PASSED|FAILED"`
Expected: test_vessel_dynamics_model PASSED(几何断言通过;VDM 现有测试仍绿 —— 验证:line 190 `x_m≈5.0·dt` 与 mass 无关;line 217 ROT 30% tolerance;line 62/79/101 monotonicity 与绝对值无关)

- [ ] **Step 7: 若 VDM 测试因新 mass/length 失败,按物理合理性更新断言(消费者回归)**

如果上一步有 VDM 测试失败,根因是 fixture 的 mass/length 改变影响了 VDM compute_izz/compute_accelerations 输出。处理原则:
- 定性断言(monotonicity/sign/bounds):应仍绿,无需改。
- 若有绝对值断言依赖旧 mass/length:按新值重算期望值(如 Izz=(1/12)·145000·45²≈2.45e7),更新断言,并在 commit message 说明"断言更新反映 P0 几何修正,非逻辑变更"。
- 若发现 VDM 行为在新参数下违反物理(如负 Izz):**停止,报告** —— 这说明 VDM 有 bug,P0 不修但记录为 P2 删除前的发现。

Run: `colcon test --packages-select l3_tdl_kernel --pytest-name test_vessel_dynamics_model 2>&1 | tail -5`
Expected: PASSED

- [ ] **Step 8: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/shared/capability_manifest.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/config/fcb_vessel_capability.yaml \
        src/l3_tdl_kernel/m5_tactical_planner/test/fixtures/fcb_capability_fixture.yaml \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_vessel_dynamics_model.cpp
git commit -m "fix(m5): correct manifest geometry to actual FCB (28m/95t -> 45m/145t)

P0 of M5 MPC redesign (DP-02/TBD-5). Manifest geometry severely mismatched
actual FCB (datasheet LOA 45m / beam 8.0m / draft 1.55m / displacement
130-160T). mass 145000 is mid-range, [TBD-HAZID] inclining calibration.
Behavior-preserving at runtime (mass/length only consumed by VDM which has
no production callers); VDM tests verified green with new values."
```

---

## Task 3: NomotoFallback 活路径回归测试(T_s 改值不影响输出)

**Files:**
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_nomoto_fallback.cpp`

**Interfaces:**
- Consumes: Task 1 产出的 `nomoto_K_s` 字段 + T_s=6.0 fixture

**目的**: 自闭环关键 —— 证明 T_s 15→6.0 不改变 NomotoFallback 活路径(r₀=0/δ=0 → 纯平移)输出。

- [ ] **Step 1: 读现有 test_nomoto_fallback.cpp,确认活路径测试结构**

Run: `grep -n "TEST_F\|TEST(\|integrate_branch\|r_rad_s\|EXPECT" src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_nomoto_fallback.cpp | head -30`

- [ ] **Step 2: 加活路径回归断言(r₀=0 → 轨迹纯平移,与 T_s 无关)**

在 test_nomoto_fallback.cpp 合适测试内(或新增 `TEST(NomotoFallback, TsChangeDoesNotAffectZeroYawRatePath)`),加:
```cpp
  // P0 回归: T_s 15->6.0 不应改变活路径(r0=0, delta=0)输出 —— 纯平移
  // r update = r - (dt/T_s)*r; r0=0 => r stays 0 regardless of T_s
  // trajectory = pure translation: x += u*cos(psi)*dt, y += u*sin(psi)*dt
  auto manifest = mass_l3::m5::shared::CapabilityManifest::load_from_yaml(kFixturePath);
  ASSERT_NEAR(manifest.config().nomoto_T_s, 6.0, 1.0e-9);  // P0 新值
  // 构造 r0=0 的输入,积分,断言 r 全程=0 且位移==u*dt(纯平移,与 T_s 无关)
  // (具体 NomotoFallback API 按现有测试模式调用;断言 r_rad_s==0, x/y==u*cos/sin(psi)*dt)
```
注:具体 NomotoFallback 调用 API 按现有 test_nomoto_fallback.cpp 模式(读 step 1 输出确认 integrate_branch 签名)。断言核心:`r_rad_s == 0.0`(全步)且 `x_m == u*cos(psi)*n*dt`(纯平移),证明 T_s 值不影响输出。

- [ ] **Step 3: 运行 test_nomoto_fallback 确认通过**

Run: `colcon build --packages-select l3_tdl_kernel --cmake-args -DM5_BUILD_TESTS=ON && colcon test --packages-select l3_tdl_kernel --event-handlers console_direct+ 2>&1 | grep -E "test_nomoto_fallback|PASSED|FAILED"`
Expected: PASSED(证明 T_s=6.0 活路径输出与 T_s 无关 —— 自闭环验证)

- [ ] **Step 4: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_nomoto_fallback.cpp
git commit -m "test(m5): NomotoFallback Ts-change regression (zero-yaw-rate path invariant)

P0 self-closure test (DP-02/TBD-5). Proves T_s 15->6.0 does not change
NomotoFallback live-path output (r0=0, delta=0 => r stays 0, trajectory is
pure translation independent of T_s). Confirms P0 value change is
behavior-preserving for the live NomotoFallback path."
```

---

## Task 4: 全量回归 + 验收门

**Files:**
- (无新文件;运行全 M5 测试 + 编译验证)

- [ ] **Step 1: 全 M5 包编译**

Run: `colcon build --packages-select l3_tdl_kernel --cmake-args -DM5_BUILD_TESTS=ON 2>&1 | tail -5`
Expected: 编译成功(6 文件改动无破坏;nomoto_K_inv_s 旧引用全消除)

- [ ] **Step 2: 全 M5 单测**

Run: `colcon test --packages-select l3_tdl_kernel --event-handlers console_direct+ 2>&1 | grep -E "PASSED|FAILED|FAIL" | tail -20`
Expected: 全部 PASSED(尤其 test_vessel_dynamics_model / test_nomoto_fallback / test_constraint_compiler / test_mid_mpc_*)

- [ ] **Step 3: 验收门核对(spec 9 条)**

逐条核对 `docs/superpowers/specs/2026-07-16-m5-p0-manifest-nomoto-fix-design.md` 的"验收边界(P0 自闭环门)":
- [ ] manifest 加载值 == spec(length 45/beam 8.0/draft 1.55/mass 145000/T_s 6.0/K_s 0.3)
- [ ] 解析单测断言解析值==yaml 值(非默认值,Task 1 step 1 断言)
- [ ] 6 文件成对更新(yaml/loader/hpp/nomoto_fallback/fixture/header 默认值)
- [ ] VDM 回归:test_vessel_dynamics_model 全绿(Task 2 step 6-7)
- [ ] NomotoFallback 回归:活路径纯平移(Task 3)
- [ ] fixture 一致性:fixture 值 == 生产 yaml 值
- [ ] manifest 消费者编译+启动不报错(Step 1 全编译)
- [ ] 无 ROS2 消息字段变化(探索已确认;本 plan 无 msg 改动)
- [ ] 编译通过(Step 1)

- [ ] **Step 4: 更新 handoff/workspace_log.md**

追加 P0 完成条目(日期/agent/commit/改动文件/测试结果/验收门状态)。

- [ ] **Step 5: Commit handoff**

```bash
git add handoff/workspace_log.md
git commit -m "docs(handoff): record P0 manifest-nomoto fix completion"
```

---

## Self-Review(plan 作者自检)

**1. Spec 覆盖**: 
- ✅ 6 文件改动 → Task 1(4 文件 nomoto)+ Task 2(4 文件 geometry,部分重叠 hpp)+ Task 3(回归)+ Task 4(验收)
- ✅ 自闭环验证表 → Task 2 step 7(VDM 物理回归)+ Task 3(NomotoFallback 活路径)
- ✅ 静默回退风险 → Task 1 step 1 断言解析值==yaml 值
- ✅ 验收门 9 条 → Task 4 step 3

**2. Placeholder 扫描**: Task 3 step 2 的 NomotoFallback API 调用标注"按现有测试模式"(因未读全 integrate_branch 签名),但给了断言核心(r==0/x==u·cos·dt);执行者读 step 1 grep 输出即可填充。其余无 TBD/TODO。

**3. 类型一致**: `nomoto_K_s`(double)在 hpp/loader/cpp/fixture/test 全程一致;`nomoto_K_s_` 成员名一致。

**4. 风险**: Task 2 step 7 是条件分支(若 VDM 测试因新 mass 失败才动),有明确处理原则(定性断言不改;绝对值按新值重算;违反物理则停止报告)。
