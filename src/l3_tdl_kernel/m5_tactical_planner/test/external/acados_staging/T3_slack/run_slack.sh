#!/usr/bin/env bash
# P1b-0 T3 -- global sigma slack mapping (exact-penalty verification) runner.
# code-gen (Python, two scenario solvers, each in its own subdir) -> build BOTH
# generated solver libs (make in each subdir) -> compile + link runner_slack.cpp
# against BOTH libs (new-ABI, -Werror) -> solve both scenarios -> assert
# exact-penalty (feasible: slack ~= 0; infeasible: slack > 0 AND relaxes the
# constraint).
#
# Each scenario exports to c_generated_code/<tag>/ (acatos overwrites a shared
# Makefile, so the two solvers MUST live in separate subdirs). The runner links
# BOTH libs (m5_staging_slack_feas + m5_staging_slack_infeas) into one binary.
#
# Link flags copied from ../T2_colreg/run_colreg.sh (verified P1b-0 line).
set -euo pipefail

cd "$(dirname "$0")"

FEAS_DIR=c_generated_code/feasible
INFEAS_DIR=c_generated_code/infeasible

echo "=== [1/3] code-gen: gen_slack.py (two scenario solvers, per-subdir) ==="
# Clear any stale c_generated_code from prior runs (the container writes these
# as root; clean here so a layout change does not collide with old artifacts).
rm -rf c_generated_code
python3 gen_slack.py

echo "=== [2/3] build both generated solver libs (make ocp_shared_lib per dir) ==="
( cd "$FEAS_DIR"   && make clean_ocp_shared_lib >/dev/null 2>&1 || true; make ocp_shared_lib )
( cd "$INFEAS_DIR" && make clean_ocp_shared_lib >/dev/null 2>&1 || true; make ocp_shared_lib )

echo "=== [3/3] compile + link runner_slack.cpp against BOTH libs (new-ABI, -Werror) ==="
g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror \
    runner_slack.cpp \
    -I"$FEAS_DIR" -I"$INFEAS_DIR" \
    -I/usr/local/include -I/usr/local/include/acados \
    -I/usr/local/include/blasfeo/include -I/usr/local/include/hpipm/include \
    -L"$FEAS_DIR"   -lacados_ocp_solver_m5_staging_slack_feas \
    -L"$INFEAS_DIR" -lacados_ocp_solver_m5_staging_slack_infeas \
    -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -Wl,-rpath,"$(pwd)/$FEAS_DIR" -Wl,-rpath,"$(pwd)/$INFEAS_DIR" \
    -Wl,-rpath,/usr/local/lib \
    -o runner_slack

echo "=== run (both scenarios) ==="
./runner_slack
