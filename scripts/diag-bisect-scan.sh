#!/usr/bin/env bash
# diag-bisect-scan.sh — portable diagnostic flow for 7-layer regression bisect.
#
# Usage: diag-bisect-scan.sh <commit-sha> <label>
#
# Performs the canonical Phase-1 evidence-gathering sequence at the given
# commit on the host worktree (which is bind-mounted into the container):
#   1. git checkout <commit> on host worktree (detached HEAD).
#   2. Overlay the portable scan test (kept in /tmp/m5-diag/portable_scan.cpp,
#      OUTSIDE the worktree) onto the test path. The overlay is unversioned
#      so it never blocks a checkout.
#   3. codegen re-run (NSH/NP/NH always logged — they are commit signature).
#   4. shared_lib rebuild (c_generated_code is git-ignored).
#   5. colcon build with M5_USE_ACADOS=ON.
#   6. Run the portable AcadosRegressionScan (8 points + sanity).
#   7. Emit a BISECT-RESULT marker for table aggregation.
set -euo pipefail

WT=/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding
COMMIT="${1:?commit sha required}"
LABEL="${2:?label required}"
CTN=codex-m5-p3-sil-nodes-1
PORTABLE=/tmp/m5-diag/portable_scan.cpp
TESTPATH=src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_acados_solver.cpp

cd "$WT"
echo "============================================================"
echo "[$(date -u +%H:%M:%S)] BISECT begin commit=$COMMIT label=$LABEL"
echo "============================================================"

# Discard any uncommitted overlay in the test file so the checkout is not
# blocked (the portable scan overlay is reapplied AFTER checkout).
git checkout HEAD -- "$TESTPATH" 2>/dev/null || true

# 1. Checkout the target commit (detached).
git checkout "$COMMIT" -- 2>&1 | tail -2 || {
  echo "ERROR: cannot checkout $COMMIT"; exit 1
}
HEAD_SHA=$(git rev-parse HEAD)
echo "HEAD now at $HEAD_SHA"

# 2. Overlay portable scan test.
cp "$PORTABLE" "$WT/$TESTPATH"
if ! grep -q "PORTABLE REGRESSION-BASELINE SCAN" "$WT/$TESTPATH"; then
  echo "ERROR: overlay failed"; exit 1
fi
echo "portable scan test overlaid"

# 2b. Apply minimal build-bypass for BUG-BUILD-01 (pre-existing in node.cpp at
# every commit under test): the #ifdef M5_USE_ACADOS dispatch-gate block
# references MidMpcSolver methods that are not implemented until C2. This
# block does NOT affect the test binary (test_mid_mpc_acados_solver does not
# link node.cpp); we disable it only so colcon build succeeds.
# The same bypass was applied in workspace_log commit a283fd1b0 (L1a batch2).
NODE=src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
if grep -q "last_nlp_backend" "$WT/$NODE" 2>/dev/null; then
  python3 - <<'PY'
import re, pathlib
p = pathlib.Path("src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp")
s = p.read_text()
# Comment out the offending block by wrapping in #if 0. Find the block by
# its unique marker.
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
    # Indent each line by one space so it's clearly commented-out, then wrap.
    replacement = (
        f"{indent}#if 0  // DIAG-BYPASS BUG-BUILD-01 (acados dispatch-gate counters not implemented until C2)\n"
        + block
        + f"\n{indent}#endif  // DIAG-BYPASS"
    )
    p.write_text(s.replace(m.group(0), replacement, 1))
    print("DIAG-BYPASS applied to node.cpp dispatch-gate block")
else:
    print("DIAG-BYPASS: dispatch-gate block not found (already disabled at this commit) — skip")
PY
fi

# 3-5. Codegen + build inside container.
docker exec "$CTN" bash -c "
  set -e
  source /opt/ros/humble/setup.bash && cd /opt/ws

  echo '=== codegen signature ==='
  python3 src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/gen_mid_mpc_acados.py 2>&1 \
    | grep -E 'NSH|NP:|NH=|idxsh|PARAM PARTITION|SOLVER OPTS' | head -10

  echo '=== shared_lib rebuild ==='
  cd src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/c_generated_code
  make clean_shared_lib >/dev/null 2>&1 || true
  make shared_lib 2>&1 | tail -2

  echo '=== colcon build ==='
  cd /opt/ws && colcon build --packages-select m5_tactical_planner --symlink-install \
    --cmake-args -DM5_USE_ACADOS=ON -DBUILD_TESTING=ON 2>&1 | tail -3
"

# 6. Run scan.
echo "=== run scan ==="
docker exec "$CTN" bash -c "
  source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash &&
  cd /opt/ws && timeout 1500 ./build/m5_tactical_planner/test_mid_mpc_acados_solver \
    --gtest_filter='AcadosRegressionScan.*' \
    --gtest_also_run_disabled_tests 2>&1 \
  | grep -E 'BSCAN' | sed \"s|^|[$LABEL] |\"
"
echo "[$(date -u +%H:%M:%S)] BISECT-RESULT commit=$COMMIT label=$LABEL done"
