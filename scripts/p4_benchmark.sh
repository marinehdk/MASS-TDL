#!/usr/bin/env bash
# P4 T1: dt benchmark — acados 1200s horizon RTI 实时性
# 头号回炉门
set -euo pipefail

REPO="/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding"
M5_DIR="${REPO}/src/l3_tdl_kernel/m5_tactical_planner"
GEN_SCRIPT="${M5_DIR}/test/external/acados_backend/gen_mid_mpc_acados.py"
FORM_HPP="${M5_DIR}/include/m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"
PARAMS_YAML="${M5_DIR}/config/m5_params.yaml"

RESULTS="${REPO}/runs/p4_dt_benchmark.txt"
echo "P4 dt benchmark — acados RTI solve_duration_ms" > "${RESULTS}"
echo "=============================================" >> "${RESULTS}"

# Budget: 10s per solve (fraction of 60s replan)
BUDGET_MS=10000

bench_n() {
    local N=$1 DT=$2 DESC=$3
    echo ""
    echo "=== Benchmark: ${DESC} (N=${N}, dt=${DT}) ==="
    
    # Patch files
    sed -i "s/^N, DT = [0-9]\+,[ ]*[0-9.]\+/N, DT = ${N}, ${DT}.0/" "${GEN_SCRIPT}"
    sed -i "s/^constexpr int32_t kAcadosNDefault = [0-9]\+/constexpr int32_t kAcadosNDefault = ${N}/" "${FORM_HPP}"
    local HORIZON=$((N * DT))
    sed -i "s/horizon_s: [0-9.]\+/horizon_s: ${HORIZON}.0/" "${PARAMS_YAML}"
    sed -i "s/n_steps: [0-9]\+/n_steps: ${N}/" "${PARAMS_YAML}"
    sed -i "s/dt_s: [0-9.]\+/dt_s: ${DT}.0/" "${PARAMS_YAML}"
    echo "  Patched: N=${N}, dt=${DT}, horizon=${HORIZON}s"
    
    # Clean and rebuild
    echo "  Building..."
    docker exec codex-m5-p3-sil-nodes-1 bash -c 'rm -rf /opt/ws/build/m5_tactical_planner' 2>/dev/null || true
    BUILD_OUT=$(docker exec codex-m5-p3-sil-nodes-1 bash -c 'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash 2>/dev/null; colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -DM5_USE_ACADOS=ON -DM5_USE_CASADI=ON 2>&1 | tail -5')
    if ! echo "${BUILD_OUT}" | grep -q "Finished"; then
        echo "  BUILD FAILED"
        echo "N=${N} dt=${DT}: BUILD_FAILED -1" >> "${RESULTS}"
        return
    fi
    
    # Run StraightLine benchmark
    echo "  Running StraightLine benchmark..."
    SL_OUT=$(docker exec codex-m5-p3-sil-nodes-1 bash -c 'cd /opt/ws && source install/setup.bash && timeout 120 ./build/m5_tactical_planner/test_mid_mpc_acados_solver --gtest_filter="AcadosSolverTest.StraightLine_ConvergesAndProducesTrajectory" 2>&1')
    echo "${SL_OUT}"
    
    SL_DUR=$(echo "${SL_OUT}" | grep -oP '\[P4-BENCH\] solve_duration_ms=\K[0-9]+' || echo "-1")
    SL_GTEST=$(echo "${SL_OUT}" | grep -oP 'OK\].*\([0-9]+ ms\)' | grep -oP '\K[0-9]+(?= ms)' || echo "-1")
    SL_STATUS=$(echo "${SL_OUT}" | grep -c "PASSED" || true)
    
    # Run RhoCalibration benchmark (realistic multi-ship)
    echo "  Running RhoCalibration benchmark..."
    RC_OUT=$(docker exec codex-m5-p3-sil-nodes-1 bash -c 'cd /opt/ws && source install/setup.bash && timeout 120 ./build/m5_tactical_planner/test_mid_mpc_acados_solver --gtest_filter="AcadosSolverTest.RhoCalibration_RealisticMultiShip" 2>&1')
    echo "${RC_OUT}"
    
    RC_DUR=$(echo "${RC_OUT}" | grep -oP 'duration_ms=\K[0-9]+' || echo "-1")
    RC_SQP=$(echo "${RC_OUT}" | grep -oP 'sqp=\K[0-9]+' || echo "-1")
    RC_STATUS=$(echo "${RC_OUT}" | grep "PASSED\|FAILED" | grep -v "Global\|FAILED 0" | head -1 || echo "PASSED")
    
    # Record
    echo "" >> "${RESULTS}"
    echo "N=${N} dt=${DT} horizon=${HORIZON}s:" >> "${RESULTS}"
    echo "  StraightLine: solve_duration_ms=${SL_DUR} gtest_ms=${SL_GTEST}" >> "${RESULTS}"
    echo "  RealisticMultiShip: solve_duration_ms=${RC_DUR} sqp_iter=${RC_SQP}" >> "${RESULTS}"
    echo "  Status: ${SL_STATUS} ${RC_STATUS}" >> "${RESULTS}"
    
    if [ "${RC_DUR}" != "-1" ] && [ "${RC_DUR}" -le "${BUDGET_MS}" ] 2>/dev/null; then
        echo "  ✅ WITHIN BUDGET (${RC_DUR}ms <= ${BUDGET_MS}ms)" >> "${RESULTS}"
        PASSED=1
    else
        echo "  ❌ OVER BUDGET (${RC_DUR}ms > ${BUDGET_MS}ms or FAILED)" >> "${RESULTS}"
        PASSED=0
    fi
}

# === Baseline: N=18, dt=5 (current) ===
bench_n 18 5.0 "baseline"

# === dt=20s N=60 (低分辨率) ===
bench_n 60 20 "dt=20s N=60"

# === dt=15s N=80 (中分辨率) ===
bench_n 80 15 "dt=15s N=80"

# === dt=10s N=120 (高分辨率) ===
bench_n 120 10 "dt=10s N=120"

# === Restore to baseline ===
sed -i "s/^N, DT = [0-9]\+,[ ]*[0-9.]\+/N, DT = 18, 5.0/" "${GEN_SCRIPT}"
sed -i "s/^constexpr int32_t kAcadosNDefault = [0-9]\+/constexpr int32_t kAcadosNDefault = 18/" "${FORM_HPP}"
sed -i "s/horizon_s: [0-9.]\+/horizon_s: 90.0/" "${PARAMS_YAML}"
sed -i "s/n_steps: [0-9]\+/n_steps: 18/" "${PARAMS_YAML}"
sed -i "s/dt_s: [0-9.]\+/dt_s: 5.0/" "${PARAMS_YAML}"

# === Summary ===
echo "" >> "${RESULTS}"
echo "=== SUMMARY ===" >> "${RESULTS}"
echo "Budget: ${BUDGET_MS}ms per solve" >> "${RESULTS}"
cat "${RESULTS}"
