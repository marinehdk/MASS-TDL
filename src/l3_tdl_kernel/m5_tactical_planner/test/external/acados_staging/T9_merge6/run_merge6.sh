#!/usr/bin/env bash
# P1b-1a Task 9 -- merged 6-point coexistence runner (the FINAL P1b-1a gate).
# code-gen (Python) -> build generated solver lib (make) -> compile + link C++
# runner under -D_GLIBCXX_USE_CXX11_ABI=1 -Werror -> solve -> assert all 6 points
# coexist (double-integrator dynamics + prefix-EQ + J_colreg EXTERNAL +
# per-target xi + CPA bound schedule).
#
# Link line copied verbatim from ../T6_doubleint/run_doubleint.sh (verified P1b-1a
# line); only the solver name + source names change (doubleint -> merge6).
set -euo pipefail

cd "$(dirname "$0")"

echo "=== [1/3] code-gen: gen_merge6.py ==="
python3 gen_merge6.py

echo "=== [2/3] build generated solver lib (make ocp_shared_lib) ==="
cd c_generated_code
make clean_ocp_shared_lib >/dev/null 2>&1 || true
make ocp_shared_lib

echo "=== [3/3] compile + link runner_merge6.cpp (new-ABI, -Werror) ==="
g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror \
    ../runner_merge6.cpp \
    -I. -I/usr/local/include -I/usr/local/include/acados \
    -I/usr/local/include/blasfeo/include -I/usr/local/include/hpipm/include \
    -L. -lacados_ocp_solver_m5_staging_merge6 \
    -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -Wl,-rpath,"$(pwd)" -Wl,-rpath,/usr/local/lib \
    -o runner_merge6

echo "=== run ==="
./runner_merge6
