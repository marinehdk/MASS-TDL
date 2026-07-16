#!/usr/bin/env bash
# P1b-1a T7 -- per-target xi high-dim slack runner.
# code-gen (Python, two scenario solvers, each in its own subdir) -> build BOTH
# generated solver libs (make in each subdir) -> compile + link runner_xi.cpp
# against BOTH libs (new-ABI, -Werror) -> solve both scenarios -> assert
# per-target xi independence (scenario 1 both feasible -> xi~0; scenario 2 A
# feasible -> xi_A~0 INDEPENDENT of B infeasible -> xi_B>0).
#
# Each scenario exports to c_generated_code/<tag>/ (acatos overwrites a shared
# Makefile, so the two solvers MUST live in separate subdirs). The runner links
# BOTH libs (m5_staging_xi_feas + m5_staging_xi_infeas) into one binary.
#
# Link line copied from ../T3_slack/run_slack.sh (verified P1b-0 line); only the
# solver names + source names change (T3 slack -> T7 xi).
set -euo pipefail

cd "$(dirname "$0")"

FEAS_DIR=c_generated_code/both_feasible
INFEAS_DIR=c_generated_code/A_feasible_B_infeasible

echo "=== [1/3] code-gen: gen_xi.py (two scenario solvers, per-subdir) ==="
# Clear any stale c_generated_code from prior runs (the container writes these
# as root; clean here so a layout change does not collide with old artifacts).
rm -rf c_generated_code
python3 gen_xi.py

echo "=== [2/3] build both generated solver libs (make ocp_shared_lib per dir) ==="
( cd "$FEAS_DIR"   && make clean_ocp_shared_lib >/dev/null 2>&1 || true; make ocp_shared_lib )
( cd "$INFEAS_DIR" && make clean_ocp_shared_lib >/dev/null 2>&1 || true; make ocp_shared_lib )

echo "=== [3/3] compile + link runner_xi.cpp against BOTH libs (new-ABI, -Werror) ==="
g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror \
    runner_xi.cpp \
    -I"$FEAS_DIR" -I"$INFEAS_DIR" \
    -I/usr/local/include -I/usr/local/include/acados \
    -I/usr/local/include/blasfeo/include -I/usr/local/include/hpipm/include \
    -L"$FEAS_DIR"   -lacados_ocp_solver_m5_staging_xi_feas \
    -L"$INFEAS_DIR" -lacados_ocp_solver_m5_staging_xi_infeas \
    -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -Wl,-rpath,"$(pwd)/$FEAS_DIR" -Wl,-rpath,"$(pwd)/$INFEAS_DIR" \
    -Wl,-rpath,/usr/local/lib \
    -o runner_xi

echo "=== run (both scenarios) ==="
./runner_xi
