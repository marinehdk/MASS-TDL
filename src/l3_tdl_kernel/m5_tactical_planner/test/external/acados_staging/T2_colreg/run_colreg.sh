#!/usr/bin/env bash
# P1b-0 T2 -- J_colreg per-stage EXTERNAL numeric equivalence runner.
# code-gen (Python) -> build generated solver lib (make) -> compile + link C++
# runner under -D_GLIBCXX_USE_CXX11_ABI=1 -Werror -> solve -> assert the staged
# EXTERNAL cost is numerically equivalent (<1e-6) to the hand-computed lumped
# production J_colreg over the solved trajectory.
#
# Link line copied verbatim from ../acados_m5_subset/run_subset.sh (verified P1a
# line); only the solver name changes (m5_subset -> m5_staging_colreg).
set -euo pipefail

cd "$(dirname "$0")"

echo "=== [1/3] code-gen: gen_colreg.py ==="
python3 gen_colreg.py

echo "=== [2/3] build generated solver lib (make ocp_shared_lib) ==="
cd c_generated_code
make clean_ocp_shared_lib >/dev/null 2>&1 || true
make ocp_shared_lib

echo "=== [3/3] compile + link runner_colreg.cpp (new-ABI, -Werror) ==="
g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror \
    ../runner_colreg.cpp \
    -I. -I/usr/local/include -I/usr/local/include/acados \
    -I/usr/local/include/blasfeo/include -I/usr/local/include/hpipm/include \
    -L. -lacados_ocp_solver_m5_staging_colreg \
    -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -Wl,-rpath,"$(pwd)" -Wl,-rpath,/usr/local/lib \
    -o runner_colreg

echo "=== run ==="
./runner_colreg
