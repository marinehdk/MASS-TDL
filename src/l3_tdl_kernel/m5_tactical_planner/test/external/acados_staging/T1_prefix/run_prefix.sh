#!/usr/bin/env bash
# P1b-0 T1 -- prefix equality staging runner.
# code-gen (Python) -> build generated solver lib (make) -> compile + link C++
# runner under -D_GLIBCXX_USE_CXX11_ABI=1 -Werror -> solve -> assert staging.
#
# Link line copied verbatim from ../acados_m5_subset/run_subset.sh (verified P1a
# line); only the solver name changes (m5_subset -> m5_staging_prefix).
set -euo pipefail

cd "$(dirname "$0")"

echo "=== [1/3] code-gen: gen_prefix.py ==="
python3 gen_prefix.py

echo "=== [2/3] build generated solver lib (make ocp_shared_lib) ==="
cd c_generated_code
make clean_ocp_shared_lib >/dev/null 2>&1 || true
make ocp_shared_lib

echo "=== [3/3] compile + link runner_prefix.cpp (new-ABI, -Werror) ==="
g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror \
    ../runner_prefix.cpp \
    -I. -I/usr/local/include -I/usr/local/include/acados \
    -I/usr/local/include/blasfeo/include -I/usr/local/include/hpipm/include \
    -L. -lacados_ocp_solver_m5_staging_prefix \
    -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -Wl,-rpath,"$(pwd)" -Wl,-rpath,/usr/local/lib \
    -o runner_prefix

echo "=== run ==="
./runner_prefix
