#!/usr/bin/env bash
# P1b-0 T4 -- bound schedule per-stage lb/ub + OR-composition runner.
# code-gen (Python) -> build generated solver lib (make) -> compile + link C++
# runner under -D_GLIBCXX_USE_CXX11_ABI=1 -Werror -> solve -> assert schedule.
#
# Link line copied verbatim from ../T1_prefix/run_prefix.sh (verified P1b-0
# line); only the solver name changes (m5_staging_prefix -> m5_staging_bounds).
set -euo pipefail

cd "$(dirname "$0")"

echo "=== [1/3] code-gen: gen_bounds.py ==="
python3 gen_bounds.py

echo "=== [2/3] build generated solver lib (make ocp_shared_lib) ==="
cd c_generated_code
make clean_ocp_shared_lib >/dev/null 2>&1 || true
make ocp_shared_lib

echo "=== [3/3] compile + link runner_bounds.cpp (new-ABI, -Werror) ==="
g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror \
    ../runner_bounds.cpp \
    -I. -I/usr/local/include -I/usr/local/include/acados \
    -I/usr/local/include/blasfeo/include -I/usr/local/include/hpipm/include \
    -L. -lacados_ocp_solver_m5_staging_bounds \
    -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -Wl,-rpath,"$(pwd)" -Wl,-rpath,/usr/local/lib \
    -o runner_bounds

echo "=== run ==="
./runner_bounds
