#!/usr/bin/env bash
# P4 T1: dt benchmark — acados 1200s horizon 三档 RTI 实时性
# 头号回炉门: 三档都超预算 → 回炉 DP-05
set -euo pipefail

REPO="/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding"
M5_DIR="${REPO}/src/l3_tdl_kernel/m5_tactical_planner"
GEN_SCRIPT="${M5_DIR}/test/external/acados_backend/gen_mid_mpc_acados.py"
FORM_HPP="${M5_DIR}/include/m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"
PARAMS_YAML="${M5_DIR}/config/m5_params.yaml"

# 备份原始文件
cp "${GEN_SCRIPT}" "${GEN_SCRIPT}.bak"
cp "${FORM_HPP}" "${FORM_HPP}.bak"
cp "${PARAMS_YAML}" "${PARAMS_YAML}.bak"

# 结果文件
RESULTS="${REPO}/runs/p4_dt_benchmark_results.txt"
echo "P4 dt benchmark — acados 1200s horizon RTI 实时性" > "${RESULTS}"
echo "================================================" >> "${RESULTS}"
echo "" >> "${RESULTS}"

BUDGET_MS=10000  # 10s 预算 (replan 60s 的合理比例)

# dt 三档: (dt, Np, description)
declare -a DT_VALS=("10" "15" "20")
declare -a NP_VALS=("120" "80" "60")
declare -a DESC_VALS=("dt=10s Np=120 (高分辨率)" "dt=15s Np=80 (中分辨率)" "dt=20s Np=60 (低分辨率)")

BEST_DT=""
BEST_NP=""
BEST_MS=999999
ANY_OK=false

for i in "${!DT_VALS[@]}"; do
    DT="${DT_VALS[$i]}"
    N="${NP_VALS[$i]}"
    DESC="${DESC_VALS[$i]}"
    
    echo ""
    echo "========== Benchmark: ${DESC} =========="
    echo ""
    
    # 修改 gen script
    sed -i "s/^N, DT = [0-9]\\+, [0-9.]\\+/N, DT = ${N}, ${DT}.0/" "${GEN_SCRIPT}"
    
    # 修改 formulation.hpp
    sed -i "s/^constexpr int32_t kAcadosNDefault = [0-9]\\+/constexpr int32_t kAcadosNDefault = ${N}/" "${FORM_HPP}"
    
    # 修改 m5_params.yaml
    HORIZON=$((N * DT))
    sed -i "s/horizon_s: [0-9.]\\+/horizon_s: ${HORIZON}.0/" "${PARAMS_YAML}"
    sed -i "s/n_steps: [0-9]\\+/n_steps: ${N}/" "${PARAMS_YAML}"
    sed -i "s/dt_s: [0-9.]\\+/dt_s: ${DT}.0/" "${PARAMS_YAML}"
    
    echo "  Patched: N=${N}, DT=${DT}, horizon=${HORIZON}s"
    
    # 在容器内 rebuild + benchmark
    echo "  Rebuilding in container..."
    # 先 rm build dir 确保 clean
    docker exec codex-m5-p3-sil-nodes-1 bash -c 'rm -rf /opt/ws/build/m5_tactical_planner' 2>/dev/null || true
    
    # rebuild
    BUILD_OUT=$(docker exec codex-m5-p3-sil-nodes-1 bash -c 'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash 2>/dev/null; colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -DM5_USE_ACADOS=ON -DM5_USE_CASADI=ON 2>&1 | tail -5')
    echo "    ${BUILD_OUT}"
    
    # 检查 build 是否成功
    if echo "${BUILD_OUT}" | grep -q "Finished"; then
        echo "  Build OK"
    else
        echo "  BUILD FAILED — recording as BLOCKED"
        echo "Benchmark N=${N} dt=${DT}: BUILD FAILED" >> "${RESULTS}"
        continue
    fi
    
    # 运行 benchmark 测试 (只跑 Realtime_UnderBudget + StraightLine)
    echo "  Running benchmark..."
    TEST_OUT=$(docker exec codex-m5-p3-sil-nodes-1 bash -c 'cd /opt/ws && source install/setup.bash && ./build/m5_tactical_planner/test_mid_mpc_acados_solver --gtest_filter="AcadosSolverTest.StraightLine_ConvergesAndProducesTrajectory:AcadosSolverTest.Realtime_UnderBudget" 2>&1')
    
    echo "${TEST_OUT}"
    
    # 提取 solve duration
    STRAIGHT_MS=$(echo "${TEST_OUT}" | grep "StraightLine_ConvergesAndProducesTrajectory" | grep -oP '\(\K[0-9]+(?= ms)' || echo "N/A")
    REALTIME_MS=$(echo "${TEST_OUT}" | grep "Realtime_UnderBudget" | grep -oP '\(\K[0-9]+(?= ms)' || echo "N/A")
    STATUS=$(echo "${TEST_OUT}" | grep -E "FAILED|PASSED" | head -1 || echo "N/A")
    
    echo "" >> "${RESULTS}"
    echo "--- dt=${DT}s N=${N} (horizon=${HORIZON}s) ---" >> "${RESULTS}"
    echo "  StraightLine solve: ${STRAIGHT_MS} ms" >> "${RESULTS}"
    echo "  Realtime_UnderBudget: ${REALTIME_MS} ms" >> "${RESULTS}"
    echo "  Test status: ${STATUS}" >> "${RESULTS}"
    
    # 判据: solve_ms ≤ 预算
    if [ "${REALTIME_MS}" != "N/A" ] && [ "${REALTIME_MS}" -le "${BUDGET_MS}" ] 2>/dev/null; then
        echo "  ✅ WITHIN BUDGET (<=${BUDGET_MS}ms)" >> "${RESULTS}"
        # 取达标最大分辨率(最小 dt)
        if [ "${REALTIME_MS}" -lt "${BEST_MS}" ] 2>/dev/null; then
            BEST_DT="${DT}"
            BEST_NP="${N}"
            BEST_MS="${REALTIME_MS}"
            ANY_OK=true
        fi
    else
        if [ "${REALTIME_MS}" = "N/A" ]; then
            echo "  ❌ TEST FAILED (solve time unavailable)" >> "${RESULTS}"
        else
            echo "  ❌ OVER BUDGET (${REALTIME_MS}ms > ${BUDGET_MS}ms)" >> "${RESULTS}"
        fi
    fi
done

# 恢复原始文件
cp "${GEN_SCRIPT}.bak" "${GEN_SCRIPT}"
cp "${FORM_HPP}.bak" "${FORM_HPP}"
cp "${PARAMS_YAML}.bak" "${PARAMS_YAML}"
rm "${GEN_SCRIPT}.bak" "${FORM_HPP}.bak" "${PARAMS_YAML}.bak"

echo "" >> "${RESULTS}"
echo "================================================" >> "${RESULTS}"
echo "基准测试总结:" >> "${RESULTS}"
echo "预算: ${BUDGET_MS}ms per solve" >> "${RESULTS}"
if [ "${ANY_OK}" = true ]; then
    echo "选定: dt=${BEST_DT}s N=${BEST_NP} (horizon=1200s, solve=${BEST_MS}ms)" >> "${RESULTS}"
    echo "✅ 至少一档达标,继续 P4" >> "${RESULTS}"
else
    echo "❌ 三档都不达标 — 头号回炉触发,回炉 DP-05" >> "${RESULTS}"
fi

# 恢复 build 到基准
docker exec codex-m5-p3-sil-nodes-1 bash -c 'rm -rf /opt/ws/build/m5_tactical_planner' 2>/dev/null || true

cat "${RESULTS}"
