#!/usr/bin/env bash
# P1b-1a Task 6 -- double-integrator heading dynamics runner (Path B).
# code-gen (Python) -> build generated solver lib (make) -> compile + link C++
# runner under -D_GLIBCXX_USE_CXX11_ABI=1 -Werror -> solve -> assert the honest
# VDM-direct double-integrator yaw channel maps cleanly to acatos disc_dyn_expr
# (forward-match < 1e-9) and SOLVES despite the marginal-stability pole at z=1.
#
# Link line copied verbatim from ../T5_merged/run_merged.sh (verified P1b-0 line);
# only the solver name changes (m5_staging_merged -> m5_staging_doubleint).
set -euo pipefail

cd "$(dirname "$0")"

echo "=== [1/3] code-gen: gen_doubleint.py ==="
python3 gen_doubleint.py

echo "=== [2/3] build generated solver lib (make ocp_shared_lib) ==="
cd c_generated_code
make clean_ocp_shared_lib >/dev/null 2>&1 || true
make ocp_shared_lib

echo "=== [3/3] compile + link runner_doubleint.cpp (new-ABI, -Werror) ==="
g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror \
    ../runner_doubleint.cpp \
    -I. -I/usr/local/include -I/usr/local/include/acados \
    -I/usr/local/include/blasfeo/include -I/usr/local/include/hpipm/include \
    -L. -lacados_ocp_solver_m5_staging_doubleint \
    -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -Wl,-rpath,"$(pwd)" -Wl,-rpath,/usr/local/lib \
    -o runner_doubleint

echo "=== run ==="
./runner_doubleint
