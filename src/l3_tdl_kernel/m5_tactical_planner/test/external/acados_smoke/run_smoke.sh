#!/usr/bin/env bash
# P1a spike — acados toolchain smoke runner.
# code-gen (Python) -> build generated solver lib (make) -> compile + link C
# runner under -D_GLIBCXX_USE_CXX11_ABI=1 -Werror -> solve -> assert converge.
set -euo pipefail

cd "$(dirname "$0")"

echo "=== [1/3] code-gen: gen_smoke.py ==="
python3 gen_smoke.py

echo "=== [2/3] build generated solver lib (make ocp_shared_lib) ==="
cd c_generated_code
# acados-generated Makefile builds libacados_ocp_solver_mass_spring.so
make clean_ocp_shared_lib >/dev/null 2>&1 || true
make ocp_shared_lib

echo "=== [3/3] compile + link smoke_runner.c (new-ABI, -Werror) ==="
# -D_GLIBCXX_USE_CXX11_ABI=1 MUST match libacados / casadi / workspace (AGENTS.md
# ABI rule). -Werror proves the link surface is ABI-clean. The acados/hpipm
# headers pull in <blasfeo_target.h> via /usr/local/include/blasfeo/include,
# matching the generated Makefile layout.
gcc -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror \
    ../smoke_runner.c \
    -I. -I/usr/local/include -I/usr/local/include/acados \
    -I/usr/local/include/blasfeo/include -I/usr/local/include/hpipm/include \
    -L. -lacados_ocp_solver_mass_spring \
    -L/usr/local/lib -lacados -lhpipm -lblasfeo -lm \
    -Wl,-rpath,"$(pwd)" -Wl,-rpath,/usr/local/lib \
    -o smoke_runner

echo "=== run ==="
./smoke_runner
