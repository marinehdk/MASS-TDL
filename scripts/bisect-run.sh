#!/usr/bin/env bash
# bisect-run.sh — git bisect run script for SQP convergence regression.
#
# Called by: git bisect run scripts/bisect-run.sh
#
# Each invocation:
#   1. Overlays the version-stable portable scan test from /tmp/m5-diag/
#   2. Applies the build bypass for node.cpp (pre-existing dispatch-gate block)
#   3. Re-runs codegen + shared_lib rebuild + colcon build in container
#   4. Runs Sanity + BisectGate tests
#   5. Returns 0 if BisectGate passes (raw=0 → good), non-zero otherwise
#
# The portable scan uses ONLY the API surface present at every commit from
# 2c031bc49 (P4) through fb84701b1 (HEAD). Each test is a fresh TEST_F
# (independent capsule) so SQP state cannot accumulate across steps.
set -euo pipefail

WT=/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding
CTN=codex-m5-p3-sil-nodes-1
PORTABLE=/tmp/m5-diag/portable_scan.cpp
TESTPATH=src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_acados_solver.cpp
NODE=src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp

cd "$WT"

# ---- Cleanup trap: restore original files before git bisect checks out ----
# the next commit. Without this, the portable-scan overlay blocks checkout.
cleanup() {
  cd "$WT"
  git checkout HEAD -- "$TESTPATH" 2>/dev/null || true
  git checkout HEAD -- "$NODE" 2>/dev/null || true
}
trap cleanup EXIT

HEAD_SHA=$(git rev-parse HEAD)
SHORT_SHA="${HEAD_SHA:0:9}"
echo "============================================================"
echo "[$(date -u +%H:%M:%S)] BISECT step at $SHORT_SHA"
echo "============================================================"

# ---- 1. Discard any prior overlay so checkout is not blocked ----
git checkout HEAD -- "$TESTPATH" 2>/dev/null || true
git checkout HEAD -- "$NODE" 2>/dev/null || true

# ---- 2. Overlay portable scan test ----
cp "$PORTABLE" "$WT/$TESTPATH"
if ! grep -q "PORTABLE REGRESSION-BASELINE SCAN" "$WT/$TESTPATH"; then
  echo "ERROR: portable scan overlay failed — missing marker"
  exit 125  # git bisect: skip this commit
fi
echo "[bisect] portable scan overlaid"

# ---- 3. Build bypass: disable dispatch-gate block in node.cpp ----
# This block references MidMpcSolver methods not implemented until C2; it does
# NOT affect the test binary (test_mid_mpc_acados_solver does not link node.cpp).
# We disable it only so colcon build succeeds.
if grep -q "last_nlp_backend" "$WT/$NODE" 2>/dev/null; then
  python3 - <<'PY'
import re, pathlib
p = pathlib.Path("src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp")
s = p.read_text()
pattern = re.compile(
    r"(\s*)(#ifdef M5_USE_ACADOS\n"
    r"\s*\{\n"
    r".*?"
    r"\s*\}\n"
    r"\s*#endif)",
    re.DOTALL,
)
m = pattern.search(s)
if m:
    indent = m.group(1)
    block = m.group(2)
    replacement = (
        f"{indent}#if 0  // DIAG-BYPASS BUG-BUILD-01 (acados dispatch-gate counters not implemented until C2)\n"
        + block
        + f"\n{indent}#endif  // DIAG-BYPASS"
    )
    p.write_text(s.replace(m.group(0), replacement, 1))
    print("[bisect] DIAG-BYPASS applied to node.cpp dispatch-gate block")
else:
    print("[bisect] DIAG-BYPASS: dispatch-gate block not found — skip")
PY
else
  echo "[bisect] DIAG-BYPASS: no dispatch-gate block at this commit — skip"
fi

# ---- 4. Codegen + shared_lib rebuild + colcon build (in container) ----
echo "[bisect] running codegen + build in container..."
if ! docker exec "$CTN" bash -c "
  set -e
  source /opt/ros/humble/setup.bash && cd /opt/ws

  echo '=== codegen ==='
  python3 src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/gen_mid_mpc_acados.py > /tmp/gen.log 2>&1
  echo \"codegen rc=\$?\"
  grep -E 'NSH|NP:|NH=|idxsh' /tmp/gen.log | head -5 || echo '(no signature lines)'

  echo '=== shared_lib rebuild ==='
  cd src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/c_generated_code
  make clean_shared_lib >/dev/null 2>&1 || true
  make shared_lib 2>&1 | tail -3

  echo '=== colcon build ==='
  cd /opt/ws && colcon build --packages-select m5_tactical_planner --symlink-install \
    --cmake-args -DM5_USE_ACADOS=ON -DBUILD_TESTING=ON 2>&1 | tail -5
"; then
  echo "ERROR: build failed at $SHORT_SHA"
  exit 125  # git bisect: skip this commit
fi

# ---- 5. Run bisect gate tests ----
echo "[bisect] running bisect gate tests..."
TEST_OUT=$(docker exec "$CTN" bash -c "
  source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash &&
  cd /opt/ws &&
  timeout 600 ./build/m5_tactical_planner/test_mid_mpc_acados_solver \
    --gtest_filter='AcadosRegressionScan.Sanity_NoTargetMustConverge:AcadosRegressionScan.BisectGate_Gap52_RawMustBeZero' \
    --gtest_color=no 2>&1
")
TEST_RC=$?

echo "$TEST_OUT" | grep -E '\[BISECT\]|\[BSCAN\]|FAILED|PASSED|ERROR|RUN|OK' || echo "$TEST_OUT" | tail -10

if [ $TEST_RC -eq 0 ]; then
  echo "[$(date -u +%H:%M:%S)] BISECT GOOD  commit=$SHORT_SHA (raw=0)"
  exit 0
else
  echo "[$(date -u +%H:%M:%S)] BISECT BAD   commit=$SHORT_SHA (raw!=0 or test failure)"
  exit 1
fi
