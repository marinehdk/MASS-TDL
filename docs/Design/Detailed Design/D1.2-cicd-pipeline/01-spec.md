# D1.2 CI/CD 5 阶段流水线 + PATH-S 强制

| 字段 | 值 |
|---|---|
| D 编号 | D1.2 |
| 阶段 | Phase 1 |
| 目标完成 | 2026-05-31 |
| 预计人周 | 1.5 |
| Owner | 基础设施负责人 |
| 关闭 finding | A P1-A8 |
| 服务 DEMO | DEMO-1 Skeleton Live 6/15 |
| 依赖 | D1.1 colcon build 验证（l3_msgs + l3_external_msgs） |

---

## §1 现状审计结论

### 1.1 `.gitlab-ci.yml`（453 行）逐阶段

| Stage | Job | 状态 | D1.2 动作 |
|---|---|---|---|
| lint | `stage-1-lint` | 真实脚本；`:108` commit-msg 仍有 `\|\| echo`（软失败残留）| 删除 `\|\| echo`，改强制 |
| lint | `multi_vessel_lint` | 真实脚本；`allow_failure: true` | 改 `false` |
| lint | `path_s_dry_run` | 真实脚本；只扫 `.py`；`allow_failure: true` | 追加 C++ grep；保持 `true`（6/15 后切强制） |
| unit-test | `stage-2-unit-test` | 真实 colcon + ASan；依赖 D1.1 workspace | 验证通过 |
| unit-test | `stage-2-pytest` | `:238` 有 `\|\| echo "not yet present"`（软失败）| 改为显式目录检测 + exit 0 |
| static-analysis | `stage-3-clang-tidy` | 真实；parallel matrix 8 包；compile_commands 依赖 colcon | violations 修干净（本 D 核心工作） |
| static-analysis | `stage-3-cppcheck-misra` | OSS fallback 已有；Premium 占位注释 | OSS 通过即可（Premium = D2.x） |
| static-analysis | `stage-3-polyspace-path-s` | license 不可用时 exit 1；已有 guard | 不动（D1.2 scope 外） |
| static-analysis | `stage-3-doer-checker-independence` | ✅ 已存在；`allow_failure: false` | 无需新建；仅补 C++ 方向于 path_s_dry_run |
| integration-test | `stage-4-integration` | 真实；依赖 `l3_integration_tests` 包 + `tests/integration/` | 验证 colcon build + 基础集成跑通 |
| release | `stage-5-release` | syft fallback `\|\| echo`（Wave 0 注释接受）| 不动 |

**关键发现**：`check-doer-checker-independence.sh` 已存在（`tools/ci/`，124 行），CI job 已 `allow_failure: false`。脚本检测 **M7→M1-M6 方向**（M7 不引用 Doer 内部头）。**缺口**：M1-M6 C++ 源文件中 `#include "m7_safety_supervisor/..."` 无检测（`path_s_dry_run` 只扫 `.py`）。

### 1.2 Docker 版本固定情况

| 项 | 当前状态 | 问题 |
|---|---|---|
| `FROM ros:jazzy-ros-base-jammy` | 无 SHA256 | 基础镜像可静默更新 |
| `gcc-14 g++-14` | 无 `=version` | apt 包漂移 |
| `clang-tidy-20 clang-format-20` | 无 `=version` | 同上 |
| `cmake` `cppcheck` `python3.10` | 无 `=version` | 同上 |
| Python pip 工具 | ✅ 已 pin（`ruff==0.6.9` 等）| 无问题 |
| `colcon-common-extensions` | 无版本（PyPI 无明确 release tag）| 接受例外，注释记录 |

### 1.3 `.clang-tidy` / `.clang-tidy.path-s` 对齐

两个文件配置正确，无需修改：
- PATH-D：LineThreshold=60，StatementThreshold=40，cognitive-complexity=10
- PATH-S：LineThreshold=**40**，StatementThreshold=30，cognitive-complexity=8，禁动态分配，禁非 const 全局变量

### 1.4 C++ 源码体量（clang-tidy 会实际执行）

| 包 | cpp/hpp 文件数 | 关键风险 |
|---|---|---|
| `m7_safety_supervisor` | 56 | `safety_supervisor_node.cpp` 387行，PATH-S LineThreshold=40，**高风险** |
| `m1_odd_envelope_manager` | 22 | PATH-S 路径，**高风险** |
| `m6_colregs_reasoner` | 56 | 体量最大，PATH-D |
| `m5_tactical_planner` | 48 | `constraint_compiler.cpp` 428行，PATH-D |
| `m8_hmi_transparency_bridge` | 28 | PATH-D |
| `m2_world_model` | 30 | PATH-D |
| `m3_mission_manager` | 24 | PATH-D |
| `m4_behavior_arbiter` | 5 | 最小，PATH-D |
| **合计** | **269** | |

---

## §2 设计

### 2.1 Docker 版本固定机制

**新增文件结构**：

```
tools/docker/
├── version-lock.txt          # 机器采集 + 人工审核，git 追踪版本变化
├── audit-versions.sh         # 在容器内运行，输出当前 apt 版本，更新 lock 文件
├── Dockerfile.ci             # 改用 ARG 消费版本，FROM 加 SHA256
└── Dockerfile.dev            # 同上
```

**`version-lock.txt` 格式**：

```
# 由 audit-versions.sh 自动填充，变更须经 code review
# 更新流程：在老容器内运行 audit-versions.sh → git commit version-lock.txt → rebuild
ROS_BASE_DIGEST=sha256:<填充>
GCC14_VER=<apt-cache show gcc-14 | grep Version>
CLANG20_VER=<apt-cache show clang-tidy-20 | grep Version>
CMAKE_VER=<apt-cache show cmake | grep Version>
CPPCHECK_VER=<apt-cache show cppcheck | grep Version>
PYTHON310_VER=<apt-cache show python3.10 | grep Version>
# colcon-common-extensions: 无明确 PyPI release tag，接受不 pin（已知例外）
```

**`audit-versions.sh` 逻辑**（新建，在宿主机有 Docker 的环境下运行）：

```bash
#!/usr/bin/env bash
# 运行方式：在宿主机执行 bash tools/docker/audit-versions.sh
# 要求：宿主机可访问 docker daemon；ros:jazzy-ros-base-jammy 已 pull
set -euo pipefail

# 从已 pull 的镜像获取 digest（宿主机执行）
DIGEST=$(docker inspect ros:jazzy-ros-base-jammy \
           --format='{{index .RepoDigests 0}}' 2>/dev/null | cut -d@ -f2)
echo "ROS_BASE_DIGEST=${DIGEST}"

# 在容器内查询 apt 版本
docker run --rm ros:jazzy-ros-base-jammy bash -c "
  apt-get update -qq
  for pkg in gcc-14 clang-tidy-20 cmake cppcheck python3.10; do
    ver=\$(apt-cache show \$pkg 2>/dev/null | grep '^Version:' | head -1 | awk '{print \$2}')
    varname=\$(echo \${pkg^^}_VER | tr '-' '_')
    echo \"\${varname}=\${ver}\"
  done
"
```

**Dockerfile.ci 修改模式**：

```dockerfile
ARG ROS_BASE_DIGEST
FROM ros:jazzy-ros-base-jammy@${ROS_BASE_DIGEST}

ARG GCC14_VER
ARG CLANG20_VER
ARG CMAKE_VER
ARG CPPCHECK_VER
ARG PYTHON310_VER

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-14=${GCC14_VER} g++-14=${GCC14_VER} \
    clang-20=${CLANG20_VER} lld-20=${CLANG20_VER} \
    clang-tidy-20=${CLANG20_VER} clang-format-20=${CLANG20_VER} \
    cmake=${CMAKE_VER} \
    cppcheck=${CPPCHECK_VER} \
    python3.10=${PYTHON310_VER} \
    ...
```

**构建命令**：`docker build --build-arg-file tools/docker/version-lock.txt -f tools/docker/Dockerfile.ci .`

### 2.2 clang-tidy violation 修复工作流

**原则**：
- 所有 violations 在源码修干净，禁止 `// NOLINT`，禁止降低阈值
- 唯一例外：第三方 ROS2 生成代码（`install/`，已通过 `HeaderFilterRegex` 排除）
- 修复顺序：PATH-S 优先（M7 → M1），再按文件体量排（M6 → M5 → M8 → M2 → M3 → M4）

**扫描命令**（在固定容器内执行，每包独立）：

```bash
docker run --rm -v $(pwd):/workspace mass-l3/ci:jazzy-ubuntu22.04 bash -c "
  source /opt/ros/jazzy/setup.bash && cd /workspace
  colcon build --packages-select \${PKG} --cmake-args -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  CONFIG=\$([ \${PKG} = m1_odd_envelope_manager ] || [ \${PKG} = m7_safety_supervisor ] \
           && echo .clang-tidy.path-s || echo .clang-tidy)
  run-clang-tidy-20 -p build/\${PKG} -config-file=\${CONFIG} \
    \$(find src/\${PKG} -name '*.cpp') 2>&1 | tee /workspace/tidy-\${PKG}.txt
  echo \"Errors: \$(grep -c 'error:' /workspace/tidy-\${PKG}.txt || echo 0)\"
"
```

**通过判据**：`grep -c "error:" tidy-${PKG}.txt` 输出为 0。

**常见修复模式**：

| 违规类型 | 修复方式 |
|---|---|
| `readability-function-size` 超 LineThreshold | 拆函数（提取私有方法） |
| `readability-function-cognitive-complexity` 超阈值 | 简化控制流，拆 helper |
| `cppcoreguidelines-no-malloc`（M7 PATH-S）| 审查确认启动后无动态分配；如有则重构为预分配 |
| `cppcoreguidelines-avoid-non-const-global-variables`（M7）| 将状态封装为类成员 |
| 命名违规（member 无 `_` 后缀 / 常量无 `k` 前缀）| 批量 rename |

### 2.3 `path_s_dry_run` C++ 扩展

在 `.gitlab-ci.yml` 现有 `path_s_dry_run` 脚本块末尾追加，**同一 job，独立 grep 段，合并 HITS**：

```yaml
    # --- C++ 边界检测（D1.2 新增）---
    - |
      grep -rn --include="*.cpp" --include="*.hpp" \
        -E '#include\s*[<"]m7_safety_supervisor/' \
        src/m1_odd_envelope_manager/ src/m2_world_model/ \
        src/m3_mission_manager/ src/m4_behavior_arbiter/ \
        src/m5_tactical_planner/ src/m6_colregs_reasoner/ \
        2>/dev/null >> path_s_report.txt || true
      CPP_HITS=$(grep -cE '\.(cpp|hpp):' path_s_report.txt 2>/dev/null || echo 0)
      echo "C++ Doer→Checker violations: $CPP_HITS"
      HITS=$((HITS + CPP_HITS))
      if [ "$HITS" -gt "0" ]; then
        echo "WARNING: Doer→Checker boundary violations detected (Python+C++)"
        cat path_s_report.txt
      else
        echo "OK: No Doer→Checker violations (Python + C++)"
      fi
```

**检测分工界面**（不重叠）：

| 检测点 | 方向 | 语言 | `allow_failure` | 关闭时点 |
|---|---|---|---|---|
| `path_s_dry_run` | M1-M6 → M7（Doer 不引用 Checker）| Python + C++ | `true` | 6/15 DEMO-1 后切 `false` |
| `check-doer-checker-independence.sh` | M7 → M1-M6（Checker 不引用 Doer）| C++ | `false`（已强制）| 已是强制 |

### 2.4 CI job 变更清单

**变更 1** — `multi_vessel_lint`（`:131`）`allow_failure: true` → `false`
- Rationale：D0 已验证 0 violations，无阻塞风险

**变更 2** — `stage-2-pytest`（`:238`）删除 `|| echo`，改显式目录检测：
```yaml
- |
  if find tests/python -name "*.py" -print -quit 2>/dev/null | grep -q .; then
    pytest tests/python/ --cov=... --junit-xml=build/pytest-junit.xml \
           --cov-fail-under=90 --timeout=120
  else
    echo "INFO: tests/python/ 不存在（Wave 0），跳过" && exit 0
  fi
```

**变更 3** — `stage-1-lint` commit-msg（`:108`）删除 `|| echo "commit-msg check optional in Wave 0"`，改为直接执行（Wave 0 注释已过期）。

**不变**：`path_s_dry_run` 保持 `allow_failure: true`（MUST-8，6/15 后切强制）。`stage-3-polyspace-path-s` 不动（license 不可用 → exit 1，D1.2 scope 外）。`stage-5-release` syft fallback 不动（Wave 0 接受）。

---

## §3 任务拆分（每 task ≤ 4h）

| ID | Task | 估时 | 输出产物 | 依赖 |
|---|---|---|---|---|
| **T1** | 编写 `audit-versions.sh`；在现有容器（或 `docker pull ros:jazzy-ros-base-jammy`）内采集 apt 版本，生成 `version-lock.txt` | 2h | `tools/docker/version-lock.txt` `tools/docker/audit-versions.sh` | — |
| **T2** | 修改 `Dockerfile.ci`：`FROM` 加 `@${ROS_BASE_DIGEST}`，apt 包加 `=${VER}` ARG | 2h | `tools/docker/Dockerfile.ci` | T1 |
| **T3** | 修改 `Dockerfile.dev`：同 T2 | 1h | `tools/docker/Dockerfile.dev` | T1 |
| **T4** | `docker build --no-cache` 验证两个 Dockerfile 均 exit 0；记录镜像 digest | 2h | 构建日志（artifact）| T2 T3 |
| **T5** | 容器内扫描 M7 + M1（PATH-S），生成 `tidy-m7.txt` `tidy-m1.txt`，记录 violation 数量与类型 | 3h | `tidy-m7.txt` `tidy-m1.txt` | T4 |
| **T6** | 修复 `m7_safety_supervisor` 所有 clang-tidy violations（函数拆分 / rename / 动态分配审查）| 4h | 修复后 M7 exit 0 | T5 |
| **T7** | 修复 `m1_odd_envelope_manager` 所有 clang-tidy violations | 3h | 修复后 M1 exit 0 | T5 |
| **T8** | 容器内扫描 M6 + M5，生成报告；修复 `m6_colregs_reasoner` violations | 4h | 修复后 M6 exit 0 | T4 |
| **T9** | 修复 `m5_tactical_planner` violations（`constraint_compiler.cpp` 428行重点）| 4h | 修复后 M5 exit 0 | T4 |
| **T10** | 容器内扫描 + 修复 M2 / M3 / M4 / M8（4 包并批，体量较小）| 4h | 4 包各 exit 0 | T4 |
| **T11** | `path_s_dry_run` 追加 C++ grep 段（§2.3）| 1h | `.gitlab-ci.yml` patch | — |
| **T12** | `multi_vessel_lint` → `allow_failure: false`；`stage-2-pytest` 软失败修复；commit-msg 强制（§2.4）| 1h | `.gitlab-ci.yml` patch | — |
| **T13** | CI 全量绿验证：在固定容器内模拟 Stage 1–3（本地），提交触发实际 pipeline；截图存档 | 4h | CI 全绿截图（DEMO-1 证据）| T6–T12 |

**总估时**：35h ≈ **1.5 人周**（按 5 天/周、7h/天 = 52.5h 产能，含 §4 风险余量）

---

## §4 Owner-by-day 矩阵（5/13–5/31）

| 日期 | 工作日 | Tasks | 备注 |
|---|---|---|---|
| 5/13 周二 | D1 | T1（apt 版本采集 + `version-lock.txt`）| D1.1 并行：ROS2 workspace 验证同期进行 |
| 5/14 周三 | D2 | T2 + T3（Dockerfile 版本 pin）| |
| 5/15 周四 | D3 | T4（docker build --no-cache 验证）| 容器固定后所有后续修复在统一环境 |
| 5/16 周五 | D4 | T5（M7+M1 扫描，出 violation 清单）| **检查点**：若 M7 violations > 50，升级风险 R2.2 |
| 5/19 周一 | D5 | T6（M7 violation 修复）| PATH-S 最严，优先完成 |
| 5/20 周二 | D6 | T7（M1 violation 修复）| |
| 5/21 周三 | D7 | T8（M6 扫描+修复）| |
| 5/22 周四 | D8 | T9（M5 修复，重点 constraint_compiler）| |
| 5/23 周五 | D9 | T10（M2/M3/M4/M8 批修复）| **检查点**：8 包全部 exit 0 |
| 5/26 周一 | D10 | T11 + T12（CI YAML 变更）| |
| 5/27 周二 | D11 | T13 前半：本地 Stage 1–3 模拟验证 | |
| 5/28 周三 | D12 | T13 后半：提交触发实际 pipeline，截图存档 | |
| 5/29–5/31 | buffer | 风险缓冲（R2.2 M7 violation 超预期 / R2.1 runner 问题）| 不规划 task，留给 contingency |

**D1.2 与 D1.1 并行期**（5/13–5/17）：D1.1 的 colcon build 验证不阻塞 Docker 版本固定工作（T1-T4 不依赖 ROS2 workspace 状态）。D1.1 的 `l3_msgs` 构建成功是 T13 Stage 2 unit-test 的前提，5/16 前 D1.1 需验证完毕。

---

## §5 依赖图

```
T1 (版本采集)
 └─→ T2 (Dockerfile.ci pin)
      └─→ T4 (docker build 验证) ──────────────────────────┐
 └─→ T3 (Dockerfile.dev pin)                               │
      └─→ T4                                               │
                                                           ▼
T5 (M7+M1 扫描) ←── T4                          T13 (CI 全量绿验证)
 └─→ T6 (M7 修复) ──────────────────────────────────→ T13
 └─→ T7 (M1 修复) ──────────────────────────────────→ T13
T8 (M6 扫描+修复) ←── T4 ──────────────────────────→ T13
T9 (M5 修复) ←── T4 ────────────────────────────────→ T13
T10 (M2/3/4/8) ←── T4 ──────────────────────────────→ T13
T11 (path_s C++ 扩展) ──────────────────────────────→ T13
T12 (YAML 变更) ─────────────────────────────────────→ T13

D1.1 l3_msgs colcon build ──────────────────────────→ T13 (Stage 2 前提)
```

---

## §6 每 task 的 acceptance criteria

| Task | Acceptance Criteria |
|---|---|
| T1 | `version-lock.txt` 存在；所有 6 个变量已填充；`audit-versions.sh` exit 0 可重复运行 |
| T2 | `Dockerfile.ci` `FROM` 含 `@sha256:`；所有 apt 包有 `=${VER}` ARG；无 `apt-get upgrade` |
| T3 | 同 T2，针对 `Dockerfile.dev` |
| T4 | `docker build --no-cache -f Dockerfile.ci .` exit 0；`docker build --no-cache -f Dockerfile.dev .` exit 0；两次构建 image digest 一致 |
| T5 | `tidy-m7.txt` 和 `tidy-m1.txt` 存在；violation 数量已记录（可以 > 0，扫描阶段）|
| T6 | `grep -c "error:" tidy-m7.txt` 输出 0（在固定容器内重新扫描确认）|
| T7 | `grep -c "error:" tidy-m1.txt` 输出 0 |
| T8 | `grep -c "error:" tidy-m6.txt` 输出 0 |
| T9 | `grep -c "error:" tidy-m5.txt` 输出 0 |
| T10 | M2/M3/M4/M8 各包 clang-tidy exit 0 |
| T11 | `path_s_dry_run` 脚本块含 C++ grep；本地执行 `bash -x` 输出 "C++ Doer→Checker violations: 0" |
| T12 | `multi_vessel_lint` 无 `allow_failure`（或 `allow_failure: false`）；`stage-2-pytest` 无 `\|\| echo`；`check-commit-msg.sh` 无 `\|\| echo` |
| T13 | GitLab pipeline 截图：5 stage 全绿（Stage 3 Polyspace 因 license 不可用 skip 可接受）；`path_s_report.txt` artifact 显示 "OK: 0 violations" |

---

## §7 Demo Charter（v3.0 §11 五段格式）

**服务 DEMO-1 Skeleton Live 2026-06-15**

**What**：CI/CD 5 阶段 pipeline 在 MASS-L3 仓库上首次全量绿——从 commit push 到 release artifact 的自动化链路完全通畅，PATH-S Doer-Checker 独立性检查（Python + C++）输出零 violations。

**When**：2026-06-15 DEMO-1 展示窗口（D1.2 实际完成目标 5/31，提前两周）。

**How**：
1. 展示 GitLab CI pipeline 页面截图：5 stage 全绿（Polyspace skip 可接受），无红色 job
2. 展示 `path_s_report.txt` artifact：`Doer→Checker violations: 0`（Python + C++ 合并）
3. 执行 `docker build --build-arg-file tools/docker/version-lock.txt -f tools/docker/Dockerfile.ci .`，展示两次构建 image digest 一致（可复现性证明）
4. 展示 `version-lock.txt` git log，说明容器版本受控

**Acceptance**：所有 stage 绿 + Doer-Checker 独立性报告 0 violations + docker build 可复现。

**Contingency**：若 GitLab runner 不可访问，以本地容器内 `colcon build` + `run-clang-tidy-20` 全包通过截图 + `path_s_dry_run` 本地执行日志代替 pipeline 截图；DEMO-1 现场标注 "local container simulation"。

---

## §8 风险 + Contingency

| ID | 风险 | 概率 | 影响 | Contingency |
|---|---|---|---|---|
| **R2.1** | GitLab CI runner 不可访问（自建 runner 配置问题 / SaaS quota）| 中 | 中 | 全部 Stage 1–3 在本地容器内用 `docker run` 模拟；Stage 4–5 接受截图为 local simulation；DEMO-1 不依赖实际 pipeline green，依赖本地容器全包 exit 0 |
| **R2.2** | M7 PATH-S violations 超出预期（> 50 个，5/16 检查点暴露）| 中 | 高 | T6 预留缓冲至 5/19 起 3 天；若仍超出，将 M7 `safety_supervisor_node.cpp` 提前拆函数（架构师-hat 介入）；不允许 NOLINT |
| **R2.3** | `m8_hmi_bridge` vs `m8_hmi_transparency_bridge` 目录并存（两个 M8 目录）| 高 | 低 | T12 前确认 CI matrix 中只有 `m8_hmi_transparency_bridge`（架构主文件 §12 标准名称）；`m8_hmi_bridge` 若为遗留目录，T13 前清理或排除 |
| **R2.4** | `colcon-common-extensions` 版本漂移 | 低 | 低 | 接受为已知例外，`version-lock.txt` 注释记录；若出现问题，切换到明确 tag fork |
| **R2.5** | M5 `constraint_compiler.cpp` 428行函数拆分引入逻辑回归 | 中 | 中 | 拆函数前确认单元测试（T9 依赖 `test_constraint_compiler.cpp` 已存在），修复后必须重跑 pytest |
| **R2.6** | `stage-2-pytest` 修改后 `tests/python/` 存在但内容不完整（partial tests）| 低 | 低 | T12 验证 `pytest ... --cov-fail-under=90` 通过；若覆盖率不足则回查 D0 pytest 基线（39 passed）|

---

## §9 D1.2 全闭判据（2026-05-31 EOD）

- [ ] `docker build --no-cache --build-arg-file tools/docker/version-lock.txt -f tools/docker/Dockerfile.ci .` exit 0，两次构建 digest 一致
- [ ] `docker build --no-cache --build-arg-file tools/docker/version-lock.txt -f tools/docker/Dockerfile.dev .` exit 0
- [ ] 所有 8 个包（M1–M8）`run-clang-tidy-20` 在固定容器内 exit 0（无 `// NOLINT`）
- [ ] `path_s_dry_run` 输出 "OK: 0 violations (Python + C++)"
- [ ] `multi_vessel_lint` `allow_failure: false`，本地跑 0 violations
- [ ] `stage-2-pytest` 无 `|| echo` 软失败
- [ ] `stage-1-lint` commit-msg 无 `|| echo` 软失败
- [ ] `m8_hmi_bridge` 目录问题解决（清理或排除）
- [ ] CI pipeline（或本地容器模拟）5 stage 全绿截图存档
- [ ] Finding A P1-A8 关闭证据：`path_s_report.txt` 显示 0 violations（Python + C++）

---

## §10 与下游 D 的接口

| 下游 D | 消费内容 | 接口约定 |
|---|---|---|
| **D1.3a** 仿真器 colcon build | `Dockerfile.ci`（固定容器）；`stage-2-unit-test` parallel matrix 可新增 `fcb_simulator` 包 | D1.2 完成后 D1.3a 直接在同一容器里构建 `fcb_simulator` 包 |
| **D1.6** 场景 CI 三层集 | `.gitlab-ci.yml` 5 阶段结构（D1.6 在 Stage 3/4 追加场景运行 jobs）| D1.2 提供干净的 YAML 基座；D1.6 不改现有 jobs，追加新 jobs |
| **D2.x** 模块代码 CI 接入 | clang-tidy 全包 exit 0 基线（D2.x 新代码必须维持）；`path_s_dry_run` C++ 检测（D2.x 写 M1-M6 C++ 时实时告警）| D2.x 开发者须在 D1.2 固定容器内本地验证 clang-tidy 通过后再提 MR |
| **D3.3a** Doer-Checker 三量化矩阵 | `check-doer-checker-independence.sh` 提供 M7 侧检测基线；D1.2 的 path_s C++ 扩展提供 Doer 侧检测 | D3.3a LOC 比 ≥ 50:1 / 圈复杂度均值比 ≥ 30:1 计算以 D1.2 修复后的代码为基准 |

---

*D1.2 spec v1.0 — 2026-05-08 brainstorming 产出*
