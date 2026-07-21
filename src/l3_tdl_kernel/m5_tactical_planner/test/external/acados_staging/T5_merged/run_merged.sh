#!/usr/bin/env bash
# P1b-0 T5 -- merged 4-point coexistence runner (the FINAL P1b-0 staging gate).
# code-gen (Python) -> build generated solver lib (make) -> compile + link C++
# runner under -D_GLIBCXX_USE_CXX11_ABI=1 -Werror -> solve -> assert all 4 points
# coexist (prefix-EQ + J_colreg EXTERNAL + sigma slack + CPA bound schedule).
#
# Link line copied verbatim from ../T4_bounds/run_bounds.sh (verified P1b-0 line);
# only the solver name changes (m5_staging_bounds -> m5_staging_merged).
set -euo pipefail

cd "$(dirname "$0")"

echo "=== [1/3] code-gen: gen_merged.py ==="
python3 gen_merged.py

echo "=== [2/3] build generated solver lib (make ocp_shared_lib) ==="
cd c_generated_code
make clean_ocp_shared_lib >/dev/null 2>&1 || true
make ocp_shared_lib

echo "=== [3/3] compile + link runner_merged.cpp (new-ABI, -Werror) ==="
g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror \
    ../runner_merged.cpp \
    -I. -I/usr/local/include -I/usr/local/include/acados \
    -I/usr/local/include/blasfeo/include -I/usr/local/include/hpipm/include \
    -L. -lacados_ocp_solver_m5_staging_merged \
    -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -Wl,-rpath,"$(pwd)" -Wl,-rpath,/usr/local/lib \
    -o runner_merged

echo "=== run ==="
./runner_merged
