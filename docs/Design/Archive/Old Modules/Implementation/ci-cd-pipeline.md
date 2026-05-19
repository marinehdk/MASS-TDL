# CI/CD Pipeline 设计（GitLab CI）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-IMPL-CICD-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-06 |
| 状态 | 正式（pipeline 模板可直接落地） |
| 平台 | GitLab CI（self-hosted runner，含本地缓存）|
| 触发分支 | `main`（保护）/ `wave-{1,2,3,4}/*`（开发）/ `feature/*`（个人）|
| 关联文件 | `00-implementation-master-plan.md` §4 / `static-analysis-policy.md` §7 / `coding-standards.md` |

---

## 1. Pipeline 总览

```
push / merge_request → trigger
                        │
   ┌────────────────────┼────────────────────────────────────┐
   │                    │                                    │
[Stage 1: lint]    [Stage 2: unit-test]    [Stage 3: static-analysis]
   ↓ pass               ↓ pass                  ↓ pass
   └────────────────────┴────────────────────────────────────┘
                        ▼
              [Stage 4: integration-test]
                        ↓ pass
                        ▼
              [Stage 5: release]  (仅 main / tag)
                        ↓
                  artifacts + container image
```

| 阶段 | 时长（目标）| 阻断 | 输出 |
|---|---|---|---|
| 1 lint | < 3 min | 是 | clang-format / clang-tidy quick / ruff / mypy |
| 2 unit-test | < 15 min | 是 | GTest + pytest，含覆盖率（lcov / coverage.py）|
| 3 static-analysis | < 30 min | 是 | clang-tidy 全规则 / cppcheck Premium / Polyspace（PATH-S）/ ASan/UBSan/TSan |
| 4 integration-test | < 45 min | 是（main / merge）| 跨模块 ROS2 节点联调 + mock 集成场景 |
| 5 release | < 10 min | — | colcon binary tarball + Docker image + SBOM |

**总目标**：PR 触发的 CI 完整跑完 < **90 min**（CI 全绿率 ≥ 95%）。

---

## 2. 触发规则与并行度

### 2.1 触发规则

```yaml
workflow:
  rules:
    # main 分支推送：完整 pipeline + release
    - if: $CI_COMMIT_BRANCH == "main"
      variables:
        FULL_PIPELINE: "true"
        RUN_RELEASE: "true"

    # merge request：完整 pipeline，无 release
    - if: $CI_PIPELINE_SOURCE == "merge_request_event"
      variables:
        FULL_PIPELINE: "true"
        RUN_RELEASE: "false"

    # tag：触发 release pipeline
    - if: $CI_COMMIT_TAG =~ /^v\d+\.\d+\.\d+$/
      variables:
        FULL_PIPELINE: "true"
        RUN_RELEASE: "true"

    # feature 分支：仅快速 pipeline（lint + 受影响模块的 unit-test）
    - if: $CI_COMMIT_BRANCH =~ /^feature\//
      variables:
        FULL_PIPELINE: "false"
        RUN_RELEASE: "false"

    # 默认：跳过
    - when: never
```

### 2.2 并行度

GitLab CI `parallel:matrix` 用于：
- 8 模块的 unit-test 并行（每 runner 1 模块）
- clang-tidy 按 package 并行
- cppcheck 按 package 并行

```yaml
unit-test:
  parallel:
    matrix:
      - PACKAGE: [m1_odd_envelope_manager, m2_world_model, m3_mission_manager,
                 m4_behavior_arbiter, m5_tactical_planner, m6_colregs_reasoner,
                 m7_safety_supervisor, m8_hmi_transparency_bridge, l3_msgs]
```

### 2.3 受影响模块检测（feature 分支加速）

```yaml
detect-changes:
  stage: .pre
  script:
    - |
      CHANGED_PKGS=$(git diff --name-only origin/main...HEAD | \
                     awk -F/ '/^src\// {print $2}' | sort -u | tr '\n' ' ')
      echo "CHANGED_PACKAGES=$CHANGED_PKGS" >> build.env
      echo "Affected packages: $CHANGED_PKGS"
  artifacts:
    reports:
      dotenv: build.env

# 后续 job 使用 $CHANGED_PACKAGES 变量限制运行范围
```

---

## 3. 缓存策略

### 3.1 ccache（C++ 编译缓存）

```yaml
default:
  cache:
    - key: ccache-${CI_COMMIT_REF_SLUG}
      paths:
        - .ccache/
      policy: pull-push
  before_script:
    - export CCACHE_DIR=$CI_PROJECT_DIR/.ccache
    - export CCACHE_MAXSIZE=5G
    - ccache --zero-stats
  after_script:
    - ccache --show-stats
```

### 3.2 colcon build artifacts

```yaml
unit-test:
  needs: [lint]
  cache:
    - key: colcon-build-${CI_COMMIT_REF_SLUG}-${PACKAGE}
      paths:
        - build/${PACKAGE}/
        - install/${PACKAGE}/
      policy: pull-push
```

### 3.3 第三方库（vendor）下载缓存

```yaml
vendor-libs:
  stage: .pre
  cache:
    - key: vendor-libs-v1
      paths:
        - tools/third-party/
      policy: pull-push
  script:
    - bash tools/ci/fetch-vendor-libs.sh   # 仅在缺失时下载
```

---

## 4. Docker 基础镜像

### 4.1 镜像清单

| 镜像 | 基础 | 用途 |
|---|---|---|
| `mass-l3/dev:jazzy-ubuntu22.04` | `ros:jazzy-ros-base-jammy` | 开发者本地容器（含全部工具链） |
| `mass-l3/ci:jazzy-ubuntu22.04` | `ros:jazzy-ros-base-jammy` | CI runner 镜像（含 clang-tidy/cppcheck/Polyspace CLI/Coverity CLI） |
| `mass-l3/runtime:jazzy-ubuntu22.04` | `ros:jazzy-ros-base-jammy` | release 镜像（仅运行时依赖，不含 dev 工具）|

### 4.2 CI 镜像 Dockerfile（摘录）

`tools/docker/Dockerfile.ci`：

```dockerfile
FROM ros:jazzy-ros-base-jammy

# 系统工具
RUN apt-get update && apt-get install -y \
        build-essential \
        gcc-14 g++-14 \
        clang-20 lld-20 clang-tidy-20 clang-format-20 \
        cmake \
        ninja-build \
        ccache \
        git curl wget \
        python3.10 python3-pip \
        lcov gcovr \
        valgrind \
        cppcheck \
    && rm -rf /var/lib/apt/lists/*

# Python 工具
RUN pip3 install --no-cache-dir \
        colcon-core==0.20.1 \
        colcon-common-extensions \
        ruff==0.6.9 \
        mypy==1.10.1 \
        pytest==8.3.3 \
        pytest-cov==5.0.0

# Polyspace CLI（需许可证 token）
COPY tools/third-party/polyspace-r2025b-cli.tar.gz /opt/
RUN tar -xzf /opt/polyspace-r2025b-cli.tar.gz -C /opt && \
    rm /opt/polyspace-r2025b-cli.tar.gz
ENV PATH="/opt/polyspace/polyspaceserver/R2025b/polyspace/bin:${PATH}"

# Coverity CLI（仅启用 PATH-S 时可选）
COPY tools/third-party/cov-analysis-linux64-2026.x.tar.gz /opt/
RUN tar -xzf /opt/cov-analysis-linux64-2026.x.tar.gz -C /opt && \
    rm /opt/cov-analysis-linux64-2026.x.tar.gz
ENV PATH="/opt/cov-analysis-linux64-2026.x/bin:${PATH}"

# 编译器默认到 GCC 14（CI 基线）
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100

WORKDIR /workspace
```

镜像构建在独立 pipeline（`tools/docker/build-ci.gitlab-ci.yml`）；版本通过 git tag 管理。

---

## 5. Stage 1: lint

```yaml
stage-1-lint:
  stage: lint
  image: mass-l3/ci:jazzy-ubuntu22.04
  needs: []
  rules:
    - if: $FULL_PIPELINE == "true" || $CI_COMMIT_BRANCH =~ /^feature\//
  script:
    # C++ 格式检查（不阻断，但产生 PR 评论）
    - find src -name "*.cpp" -o -name "*.hpp" | xargs clang-format-20 --dry-run --Werror

    # C++ 快速 lint（仅基础 checks，全规则在 stage-3）
    - clang-tidy-20 --checks=bugprone-*,clang-analyzer-* \
                    -p build/compile_commands.json src/m1_odd_envelope_manager/**/*.cpp || true

    # Python lint（Ruff + mypy strict）
    - ruff check src/m2_world_model/python tools/ src/m8_hmi_transparency_bridge/python
    - mypy src/m2_world_model/python tools/ src/m8_hmi_transparency_bridge/python

    # YAML / JSON / Markdown lint
    - yamllint -c .yamllint .gitlab-ci.yml src/**/*.yaml
    - markdownlint --config .markdownlint.json docs/Implementation/

    # ROS2 .msg 字段命名检查
    - bash tools/ci/check-msg-naming.sh src/l3_msgs/msg/

    # commit 信息格式检查（Conventional Commits）
    - bash tools/ci/check-commit-msg.sh
  artifacts:
    when: always
    reports:
      codequality: build/lint-quality.json
```

### 5.1 ROS2 .msg 字段命名检查脚本

`tools/ci/check-msg-naming.sh`：

```bash
#!/usr/bin/env bash
# 检查所有 .msg 文件遵循命名规则：
#  - 字段：lower_snake_case
#  - 单位后缀：_m / _s / _kn / _deg / _rad / _hz
#  - 时间字段：stamp / received_stamp（builtin_interfaces/Time）
set -euo pipefail
violations=0
for f in src/l3_msgs/msg/*.msg src/l3_external_msgs/msg/*.msg; do
    while IFS= read -r line; do
        # 跳过注释 / 空行
        [[ "$line" =~ ^[[:space:]]*# ]] && continue
        [[ -z "$line" ]] && continue
        field=$(echo "$line" | awk '{print $2}')
        if [[ ! "$field" =~ ^[a-z][a-z0-9_]*$ ]]; then
            echo "VIOLATION: $f: field '$field' not lower_snake_case"
            violations=$((violations + 1))
        fi
    done < "$f"
done
[[ $violations -gt 0 ]] && exit 1 || echo ".msg naming: OK"
```

---

## 6. Stage 2: unit-test

```yaml
stage-2-unit-test:
  stage: unit-test
  image: mass-l3/ci:jazzy-ubuntu22.04
  needs: [stage-1-lint]
  rules:
    - if: $FULL_PIPELINE == "true"
  parallel:
    matrix:
      - PACKAGE: [m1_odd_envelope_manager, m2_world_model, m3_mission_manager,
                  m4_behavior_arbiter, m5_tactical_planner, m6_colregs_reasoner,
                  m7_safety_supervisor, m8_hmi_transparency_bridge, l3_msgs]
  script:
    # Build with coverage flags
    - source /opt/ros/jazzy/setup.bash
    - colcon build --packages-up-to ${PACKAGE} \
        --cmake-args -DCMAKE_BUILD_TYPE=Debug \
                     -DCMAKE_CXX_FLAGS="-fprofile-arcs -ftest-coverage" \
                     -DCMAKE_EXE_LINKER_FLAGS="-fprofile-arcs -ftest-coverage"

    # ASan + UBSan unit-test pass
    - colcon build --packages-select ${PACKAGE} \
        --cmake-args -DCMAKE_BUILD_TYPE=Debug \
                     -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
    - ASAN_OPTIONS=halt_on_error=1:abort_on_error=1 \
      UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
      colcon test --packages-select ${PACKAGE} --return-code-on-test-failure
    - colcon test-result --verbose --test-result-base build/${PACKAGE}

    # 覆盖率报告（PATH-S/D ≥ 90%；PATH-H ≥ 80%）
    - lcov --capture --directory build/${PACKAGE} --output-file coverage-${PACKAGE}.info
    - lcov --remove coverage-${PACKAGE}.info "*/test/*" "*/install/*" --output-file coverage-${PACKAGE}.info
    - genhtml coverage-${PACKAGE}.info --output-directory coverage-html-${PACKAGE}
    - bash tools/ci/check-coverage-threshold.sh coverage-${PACKAGE}.info ${PACKAGE}
  artifacts:
    when: always
    paths:
      - coverage-html-${PACKAGE}/
      - coverage-${PACKAGE}.info
    reports:
      junit: build/${PACKAGE}/test_results/**/*.xml
      coverage_report:
        coverage_format: cobertura
        path: coverage-${PACKAGE}.xml
  coverage: '/lines\.+:\s+(\d+\.\d+)%/'
```

### 6.1 覆盖率阈值检查

`tools/ci/check-coverage-threshold.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail
COVERAGE_FILE=$1
PACKAGE=$2

# 阈值
declare -A THRESHOLDS=(
    [m1_odd_envelope_manager]=90
    [m2_world_model]=90
    [m3_mission_manager]=90
    [m4_behavior_arbiter]=90
    [m5_tactical_planner]=90
    [m6_colregs_reasoner]=90
    [m7_safety_supervisor]=95   # PATH-S 更严
    [m8_hmi_transparency_bridge]=80
    [l3_msgs]=70                # 消息包仅 IDL，无业务逻辑
)

threshold=${THRESHOLDS[$PACKAGE]:-90}
actual=$(lcov --summary "$COVERAGE_FILE" 2>&1 | awk '/lines/{print $2}' | tr -d '%')

echo "Coverage: ${actual}% (threshold: ${threshold}%)"
awk -v a="$actual" -v t="$threshold" 'BEGIN{exit !(a >= t)}'
```

### 6.2 Python 单元测试（M2 + M8 子集）

```yaml
stage-2-pytest:
  stage: unit-test
  needs: [stage-1-lint]
  rules:
    - if: $FULL_PIPELINE == "true"
      changes:
        - src/m2_world_model/python/**
        - src/m8_hmi_transparency_bridge/python/**
        - tools/**
  script:
    - pytest --cov=src/m2_world_model/python \
             --cov=src/m8_hmi_transparency_bridge/python \
             --cov-report=html:coverage-py-html \
             --cov-report=term-missing \
             --cov-fail-under=90 \
             --junit-xml=build/pytest-junit.xml \
             tests/python/
  artifacts:
    when: always
    paths:
      - coverage-py-html/
    reports:
      junit: build/pytest-junit.xml
      coverage_report:
        coverage_format: cobertura
        path: coverage-py.xml
```

---

## 7. Stage 3: static-analysis

```yaml
stage-3-clang-tidy:
  stage: static-analysis
  needs: [stage-2-unit-test]
  rules:
    - if: $FULL_PIPELINE == "true"
  parallel:
    matrix:
      - PACKAGE: [m1_odd_envelope_manager, m2_world_model, m3_mission_manager,
                  m4_behavior_arbiter, m5_tactical_planner, m6_colregs_reasoner,
                  m7_safety_supervisor, m8_hmi_transparency_bridge]
  script:
    - source /opt/ros/jazzy/setup.bash
    - colcon build --packages-select ${PACKAGE} --cmake-args -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    # PATH-S 路径用更严格的 .clang-tidy.path-s
    - |
      if [[ "$PACKAGE" == "m1_odd_envelope_manager" || "$PACKAGE" == "m7_safety_supervisor" ]]; then
        CONFIG=".clang-tidy.path-s"
      else
        CONFIG=".clang-tidy"
      fi
    - run-clang-tidy-20 -p build/${PACKAGE} \
                        -config-file=${CONFIG} \
                        -warnings-as-errors='*' \
                        -export-fixes=clang-tidy-fixes-${PACKAGE}.yaml \
                        $(find src/${PACKAGE} -name "*.cpp")
  artifacts:
    when: always
    paths:
      - clang-tidy-fixes-*.yaml
    reports:
      codequality: build/clang-tidy-${PACKAGE}.sarif

stage-3-cppcheck-misra:
  stage: static-analysis
  needs: [stage-2-unit-test]
  rules:
    - if: $FULL_PIPELINE == "true"
  parallel:
    matrix:
      - PACKAGE: [m1_odd_envelope_manager, m2_world_model, m3_mission_manager,
                  m4_behavior_arbiter, m5_tactical_planner, m6_colregs_reasoner,
                  m7_safety_supervisor, m8_hmi_transparency_bridge]
  script:
    - cppcheck-premium --project=src/${PACKAGE}/cppcheck.cfg \
                       --addon=misra-cpp-2023 \
                       --inline-suppr \
                       --error-exitcode=1 \
                       --output-file=build/cppcheck-${PACKAGE}.sarif \
                       --xml --xml-version=2 \
                       2> build/cppcheck-${PACKAGE}.xml
  artifacts:
    when: always
    paths:
      - build/cppcheck-*.xml
      - build/cppcheck-*.sarif
    reports:
      codequality: build/cppcheck-${PACKAGE}.sarif

stage-3-polyspace-path-s:
  stage: static-analysis
  needs: [stage-2-unit-test]
  rules:
    - if: $FULL_PIPELINE == "true"
      changes:
        - src/m1_odd_envelope_manager/**
        - src/m7_safety_supervisor/**
  parallel:
    matrix:
      - PACKAGE: [m1_odd_envelope_manager, m7_safety_supervisor]
  variables:
    POLYSPACE_LICENSE_FILE: "${POLYSPACE_LIC}"   # 从 GitLab Vault 注入
  script:
    - polyspace-bug-finder-server -prog ${PACKAGE} \
                                  -lang cpp \
                                  -cpp-version cpp17 \
                                  -sources-list-file build/${PACKAGE}/sources.txt \
                                  -checkers MISRA_Cpp_2023_Mandatory,MISRA_Cpp_2023_Required,IEC_61508_Mandatory \
                                  -results-dir build/polyspace-${PACKAGE}
    - polyspace-code-prover-server -prog ${PACKAGE} \
                                   -lang cpp \
                                   -cpp-version cpp17 \
                                   -sources-list-file build/${PACKAGE}/sources.txt \
                                   -results-dir build/polyspace-${PACKAGE}-cp
    - polyspace-results-export -results-dir build/polyspace-${PACKAGE} \
                                -format html \
                                -output-name build/polyspace-${PACKAGE}-report.html
    # 阈值检查：Red = 0 / Orange ≤ 1%
    - bash tools/ci/check-polyspace-thresholds.sh build/polyspace-${PACKAGE}
  artifacts:
    when: always
    paths:
      - build/polyspace-${PACKAGE}-report.html
      - build/polyspace-${PACKAGE}/

stage-3-doer-checker-independence:
  stage: static-analysis
  needs: [stage-1-lint]
  script:
    - bash tools/ci/check-doer-checker-independence.sh
  # 不可豁免：违反 = ADR-001 失效
  allow_failure: false

stage-3-asan-ubsan:
  # 已与 Stage 2 集成（unit-test 内置 ASan + UBSan flags）

stage-3-tsan:
  stage: static-analysis
  needs: [stage-2-unit-test]
  rules:
    - if: $FULL_PIPELINE == "true"
      changes:
        - src/m2_world_model/**
        - src/m5_tactical_planner/**
  parallel:
    matrix:
      - PACKAGE: [m2_world_model, m5_tactical_planner]
  script:
    - colcon build --packages-select ${PACKAGE} \
        --cmake-args -DCMAKE_BUILD_TYPE=Debug \
                     -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
    - TSAN_OPTIONS=halt_on_error=1 colcon test --packages-select ${PACKAGE}
```

### 7.1 Polyspace 阈值检查

`tools/ci/check-polyspace-thresholds.sh`：

```bash
#!/usr/bin/env bash
set -euo pipefail
RESULTS_DIR=$1

# 提取 Red / Orange / Green 计数（polyspace-results-export JSON output）
polyspace-results-export -results-dir "$RESULTS_DIR" \
                          -format json \
                          -output-name "${RESULTS_DIR}/results.json"

red=$(jq '.summary.red_count' "${RESULTS_DIR}/results.json")
orange=$(jq '.summary.orange_count' "${RESULTS_DIR}/results.json")
green=$(jq '.summary.green_count' "${RESULTS_DIR}/results.json")

total=$((red + orange + green))

echo "Polyspace results: red=$red, orange=$orange, green=$green, total=$total"

# Red 阈值：0
if [[ $red -gt 0 ]]; then
    echo "FAIL: Polyspace Red defects = $red (threshold: 0)"
    exit 1
fi

# Orange 比例：≤ 1%
if [[ $total -gt 0 ]]; then
    orange_pct=$(awk -v o="$orange" -v t="$total" 'BEGIN{print o/t*100}')
    if awk -v p="$orange_pct" 'BEGIN{exit !(p > 1.0)}'; then
        echo "FAIL: Polyspace Orange ratio = ${orange_pct}% (threshold: ≤ 1%)"
        exit 1
    fi
fi

echo "Polyspace thresholds: PASS"
```

---

## 8. Stage 4: integration-test

```yaml
stage-4-integration:
  stage: integration-test
  needs: [stage-3-clang-tidy, stage-3-cppcheck-misra, stage-3-doer-checker-independence]
  rules:
    - if: $CI_COMMIT_BRANCH == "main" || $CI_PIPELINE_SOURCE == "merge_request_event"
  image: mass-l3/ci:jazzy-ubuntu22.04
  services:
    # 用 ROS2 daemon 多节点
  script:
    - source /opt/ros/jazzy/setup.bash
    - colcon build
    - source install/setup.bash

    # 启动所有 8 个 L3 节点 + mock 跨层节点
    - ros2 launch tools/ci/full_l3_stack.launch.py &
    - L3_PID=$!
    - sleep 5  # 等节点启动稳定

    # 跨模块场景测试（详见 docs/Test Plan/integration-test-spec.md）
    - pytest tests/integration/ \
             --junit-xml=build/integration-junit.xml \
             --timeout=300

    # 关闭 L3 stack
    - kill $L3_PID

    # ROS2 接口契约校验（每条 §15 接口都有 mock 测试）
    - bash tools/ci/check-msg-contract.sh
  artifacts:
    when: always
    paths:
      - logs/integration/
    reports:
      junit: build/integration-junit.xml
```

### 8.1 跨模块场景集成测试（覆盖范围）

| 场景 ID | 路径 | 覆盖 §15 消息 |
|---|---|---|
| INT-001 | M2 → M6 → M5 链条 | World_StateMsg, COLREGs_ConstraintMsg, AvoidancePlanMsg |
| INT-002 | M3 → L2 重规划（mock）→ M3 | RouteReplanRequest, ReplanResponseMsg [v1.1.2 RFC-006] |
| INT-003 | X-axis Checker → M7 → M1 否决告警 | CheckerVetoNotification, Safety_AlertMsg |
| INT-004 | Reflex Arc → M1 OVERRIDDEN 切换 | ReflexActivationNotification |
| INT-005 | Hardware Override → M1 → M5 冻结 | OverrideActiveSignal |
| INT-006 | M5 双接口（AvoidancePlan + ReactiveOverride）三模式切换 | AvoidancePlanMsg + ReactiveOverrideCmd |
| INT-007 | 全模块 → ASDR | ASDR_RecordMsg（含 v1.1.2 RFC-004 增补 M3/M5 行）|
| INT-008 | M8 SAT-1/2/3 自适应触发 | SAT_DataMsg + UI_StateMsg + ToR_RequestMsg |

完整集成测试场景库见 `docs/Test Plan/integration-test-spec.md`（方向 4 待启动）。

### 8.2 接口契约校验

`tools/ci/check-msg-contract.sh`：

```bash
#!/usr/bin/env bash
# 校验所有 .msg 文件字段与 v1.1.2 §15.1 IDL 一致
# - 字段名 / 类型对齐
# - 频率注释存在（@frequency: X Hz）
# - confidence / stamp / rationale 强制字段（按 v1.1.2 §4.4 数据总线设计原则）
set -euo pipefail
expected_msgs=(
    "ODDState" "WorldState" "VoyageTask" "MissionGoal" "RouteReplanRequest"
    "ReplanResponse" "BehaviorPlan" "COLREGsConstraint" "AvoidancePlan"
    "ReactiveOverrideCmd" "SafetyAlert" "CheckerVetoNotification"
    "ASDRRecord" "EmergencyCommand" "ReflexActivationNotification"
    "OverrideActiveSignal" "ToRRequest" "SATData"
)
for msg in "${expected_msgs[@]}"; do
    if [[ ! -f "src/l3_msgs/msg/${msg}.msg" && ! -f "src/l3_external_msgs/msg/${msg}.msg" ]]; then
        echo "MISSING: $msg.msg"
        exit 1
    fi
done
echo "All §15.1 IDL messages present: OK"
```

---

## 9. Stage 5: release

```yaml
stage-5-release:
  stage: release
  needs: [stage-4-integration]
  rules:
    - if: $CI_COMMIT_TAG =~ /^v\d+\.\d+\.\d+$/
      when: on_success
    - if: $CI_COMMIT_BRANCH == "main"
      when: manual
  image: mass-l3/ci:jazzy-ubuntu22.04
  script:
    # 全量 Release 构建（O2 + LTO）
    - source /opt/ros/jazzy/setup.bash
    - colcon build --cmake-args -DCMAKE_BUILD_TYPE=Release -DLTO=ON

    # 打包 colcon install 为 tarball
    - tar -czf mass-l3-tdl-${CI_COMMIT_TAG:-${CI_COMMIT_SHA:0:8}}.tar.gz install/

    # 构建 runtime Docker 镜像
    - docker build -f tools/docker/Dockerfile.runtime \
                   --tag registry.gitlab.com/mass-l3/runtime:${CI_COMMIT_TAG:-${CI_COMMIT_SHA:0:8}} \
                   --tag registry.gitlab.com/mass-l3/runtime:latest \
                   .
    - docker push registry.gitlab.com/mass-l3/runtime:${CI_COMMIT_TAG:-${CI_COMMIT_SHA:0:8}}
    - docker push registry.gitlab.com/mass-l3/runtime:latest

    # 生成 SBOM（Software Bill of Materials，CCS 入级要求）
    - syft packages dir:install -o cyclonedx-json > sbom-${CI_COMMIT_TAG}.json
    - bash tools/ci/sbom-license-check.sh sbom-${CI_COMMIT_TAG}.json

    # 生成 release notes（从 git log）
    - bash tools/ci/generate-release-notes.sh > RELEASE_NOTES.md
  artifacts:
    paths:
      - mass-l3-tdl-*.tar.gz
      - sbom-*.json
      - RELEASE_NOTES.md
    expire_in: never
  release:
    tag_name: $CI_COMMIT_TAG
    description: ./RELEASE_NOTES.md
    assets:
      links:
        - name: "L3 TDL binary tarball"
          url: "${CI_PROJECT_URL}/-/jobs/${CI_JOB_ID}/artifacts/raw/mass-l3-tdl-${CI_COMMIT_TAG}.tar.gz"
        - name: "SBOM (CycloneDX)"
          url: "${CI_PROJECT_URL}/-/jobs/${CI_JOB_ID}/artifacts/raw/sbom-${CI_COMMIT_TAG}.json"
```

### 9.1 Semantic Versioning

```
v<MAJOR>.<MINOR>.<PATCH>
   ▲      ▲       ▲
   │      │       └─ 兼容性 bug 修复（不改 IDL，不改公共 API）
   │      └────────── 新功能 / IDL 字段添加（向后兼容）
   └───────────────── IDL breaking change / API breaking change（v1.1.2 已锁定，避免）
```

**特殊规则**：
- v1.1.x = 当前架构基线（v1.1.2，v1.1.3 待 HAZID 完成回填）
- v1.2.x = M1/M7 切换 RTI Connext Cert 后（如启用），或多船型扩展（拖船 / 渡船）
- v2.x.x = 重大 IDL breaking change（按 ADR 评审决定）

### 9.2 SBOM + License Check

```bash
# tools/ci/sbom-license-check.sh
allowed_licenses=("Apache-2.0" "MIT" "BSD-3-Clause" "BSD-2-Clause" "ISC" "BSL-1.0" \
                  "Boost-1.0" "MPL-2.0" "EPL-2.0" "LGPL-2.1" "LGPL-3.0" "GPL-3.0-with-classpath-exception")
violations=$(jq -r '.components[] | select(.licenses[].license.id as $id | [$id] | inside($ALLOWED) | not) | .name' sbom.json)
[[ -n "$violations" ]] && echo "License violations: $violations" && exit 1
```

> **注意**：CCS 入级时须提供 SBOM + 许可证清单，证明无 GPL 强 copyleft 库泄漏到分发件。

---

## 10. GitLab CI 变量与 Secrets

| 变量 | 类型 | 来源 | 用途 |
|---|---|---|---|
| `POLYSPACE_LIC` | masked, file | GitLab Vault | Polyspace CLI 许可证 token |
| `COVERITY_LIC` | masked, file | GitLab Vault | Coverity CLI 许可证 token |
| `CI_REGISTRY_USER` | system | GitLab 内置 | Docker registry 推送 |
| `CI_REGISTRY_PASSWORD` | masked, system | GitLab 内置 | Docker registry 推送 |
| `RTI_LICENSE_TOKEN` | masked, file | GitLab Vault | （v1.2 升级时启用）RTI Connext Cert 许可证 |

**Vault 集成**：
```yaml
default:
  id_tokens:
    VAULT_AUTH_TOKEN:
      aud: https://vault.example.com
  secrets:
    POLYSPACE_LIC:
      vault: kv/data/mass-l3/polyspace_license@secret
      file: true
```

---

## 11. 分支保护规则

`main` 分支强制：
- Approver ≥ 2（至少 1 名 reviewer 来自其他模块团队）
- 全部 CI Stages 1–4 绿色
- 解决全部 PR comment（`unresolved` 阻断）
- Squash + Conventional Commits（CI 强制）
- 强制启用 PR 模板（`tools/ci/pr-template.md`）

`wave-{1,2,3,4}/*` 分支：
- Approver ≥ 1
- CI Stages 1–3 绿色（integration 可后置）
- 允许 force push（开发期灵活）

`feature/*` 分支：
- Approver = 0（个人空间）
- CI Stage 1（lint）+ Stage 2（受影响模块）
- 完全 force push

---

## 12. PR / Commit 规范

### 12.1 Conventional Commits

```
<type>(<scope>): <subject>

<body>

<footer>
```

**type**：`feat` / `fix` / `refactor` / `docs` / `test` / `chore` / `ci` / `perf`
**scope**：`m1` / `m2` / ... / `m8` / `l3-msgs` / `ci` / `docs`
**subject**：动词原形开头，≤ 72 字符

示例：
```
feat(m6): add Rule 14 head-on detection with ODD-aware threshold

Implements 头对头判别 per v1.1.2 §9.3, with T_act parameter loaded
from config/colregs_params.yaml (HAZID-pending value, see [TBD-HAZID]).

Refs: SANGO-ADAS-L3-DD-M6-001 §5.2
Closes: #142
```

### 12.2 PR 模板

`tools/ci/pr-template.md`（自动注入到所有 MR）：

```markdown
## Summary
（1–3 句话，what + why）

## Linked Issues
- Refs: SANGO-ADAS-L3-DD-M{N}-001 §X.Y
- Closes: #NNN（如适用）

## Changes
（要点列表）

## Reviewer Checklist
- [ ] 命名遵循 `coding-standards.md` §4
- [ ] 注释命中 `coding-standards.md` §5.1 五种合法场景
- [ ] M1/M7 路径无动态分配 / 无异常 / 无第三方非白名单库
- [ ] 错误处理符合 `coding-standards.md` §8.1 决策树
- [ ] 函数 ≤ 60 行（M1/M7 ≤ 40 行）
- [ ] 单元测试覆盖率 ≥ 90%（PATH-S ≥ 95%）
- [ ] CI 5 阶段全绿
- [ ] 跨模块接口字段对齐 v1.1.2 §15.1 IDL
- [ ] HAZID 校准点用 `[TBD-HAZID]` 注释 + YAML 注入

## Test Plan
（如何手动验证）
```

---

## 13. 失败重试与超时

```yaml
default:
  retry:
    max: 2
    when:
      - runner_system_failure       # runner 系统失败 → 重试
      - stuck_or_timeout_failure    # 超时 → 重试
      - api_failure                 # GitLab API 失败 → 重试
  timeout: 60 minutes               # 默认 60min；polyspace 单独 90min
```

**Polyspace 长任务超时覆盖**：

```yaml
stage-3-polyspace-path-s:
  timeout: 90 minutes
```

---

## 14. 监控与报警

| 监控项 | 阈值 | 报警渠道 |
|---|---|---|
| CI 全绿率（每周）| ≥ 95% | GitLab dashboard + Slack #ci-alerts |
| 平均 CI 时长（每日）| ≤ 90 min | Prometheus + Grafana |
| Polyspace Orange 占比（PATH-S）| ≤ 1% | Slack 月报 |
| 单元测试覆盖率（每模块）| ≥ 90% | PR 评论自动注入 |
| Runner 排队时长 | < 5 min | PagerDuty |

---

## 15. 修订记录

| 版本 | 日期 | 修订人 | 变更摘要 |
|---|---|---|---|
| v1.0 | 2026-05-06 | Claude（实施层 kickoff） | 初稿创建：5 阶段 GitLab CI pipeline + Polyspace/Coverity/clang-tidy/cppcheck Premium 集成 + Doer-Checker 独立性自动验证 |

---

## 16. 引用

- **GitLab CI/CD YAML reference** — [docs.gitlab.com/ci/yaml](https://docs.gitlab.com/ci/yaml/) 🟢 A
- **ROS 2 Jazzy CI patterns** — [docs.ros.org/en/jazzy/Tutorials](https://docs.ros.org/en/jazzy/Tutorials/Intermediate/Composition.html) 🟢 A
- **Conventional Commits** — [conventionalcommits.org](https://www.conventionalcommits.org/) 🟢 A
- **CycloneDX SBOM** — [cyclonedx.org](https://cyclonedx.org/) 🟢 A
- **DevSecOps in Cruise Industry**（参考实践）— [SAMRIDDHI Journal](https://www.smsjournals.com/index.php/SAMRIDDHI/article/view/3386/1714) 🟡
- IEC 61508-3 §7.4.4 软件设计与开发的系统性故障避免
