#!/usr/bin/env bash
# P1b-1a T8 -- orchestrate c(u) yaw-gain + integrator model-class identification
# (Path B: compile -> VDM zigzag sim -> c(u) fit -> verify).
#
# NOT production code. test/external staging spike only.
#
# REAL link recipe (the brief's compile line is a placeholder). Linking the
# WHOLE m5 static archive drags in trajectory_propagator/time_alignment objects
# that reference rclcpp::Time (pulling the entire ament tree). The ONLY two
# objects ident_runner needs -- vessel_dynamics_model.cpp.o and
# capability_manifest.cpp.o -- have NO rclcpp deps. So extract those two .o
# files from the archive with `ar x` and link them directly.
set -euo pipefail

cd "$(dirname "$0")"

# Worktree root inside the container (./src -> /opt/ws/src in docker-compose).
# ident_runner's default fixture path is relative to the worktree root, so run
# the binary from there.
WORKTREE_ROOT="${WORKTREE_ROOT:-/opt/ws/src/l3_tdl_kernel/m5_tactical_planner}"
WORKTREE_ROOT="$(cd "$WORKTREE_ROOT" && pwd)"

M5_INSTALL="${M5_INSTALL:-/opt/ws/install/m5_tactical_planner}"
FIXTURE="${WORKTREE_ROOT}/test/fixtures/fcb_capability_fixture.yaml"

# ---------------------------------------------------------------- [1/4] compile
echo "=== [1/4] compile ident_runner (real VDM, 2-object link) ==="
OBJDIR="$(mktemp -d)"
trap 'rm -rf "$OBJDIR"' EXIT

# `ar x` writes into cwd; cd into the temp dir to keep the source tree clean.
( cd "$OBJDIR" && ar x "${M5_INSTALL}/lib/libm5_shared_lib.a" \
    vessel_dynamics_model.cpp.o capability_manifest.cpp.o )

# Include paths:
#   - M5_INSTALL/include  (-I) -> m5_tactical_planner headers (vessel_dynamics_
#     model.hpp -> types.hpp pulls Eigen/Dense + the ROS msg
#     l3_msgs/msg/avoidance_plan.hpp, so those must resolve at COMPILE time).
#   - /usr/include/eigen3 (-I) -> Eigen (for types.hpp).
#   - A SMALL allowlist of ROS humble package roots -> the avoidance_plan.hpp
#     msg header transitively needs only these packages (verified from its
#     detail headers): l3_msgs, builtin_interfaces, rosidl_runtime_cpp,
#     rosidl_runtime_c, rosidl_typesupport_cpp, rosidl_typesupport_interface,
#     rosidl_generator_cpp. We add ONLY those roots by name.
# These are COMPILE-ONLY: the link step uses just the two rclcpp-free .o files,
# so no ament/rclcpp library is linked.
#
# IMPORTANT: do NOT blanket-glob /opt/ros/humble/include/*. Some ROS package
# roots have short names (e.g. "idl", "string") whose headers shadow system
# <string.h>/<math.h> and break the toolchain headers (mathcalls.h
# "missing binary operator", idl/string.h). An explicit allowlist avoids that.
INCLUDES=(-I"${M5_INSTALL}/include" -I/usr/include/eigen3)
ROS_PKGS=(l3_msgs builtin_interfaces geographic_msgs rosidl_runtime_cpp \
          rosidl_runtime_c rosidl_typesupport_cpp rosidl_typesupport_interface \
          rosidl_generator_cpp)
for pkg in "${ROS_PKGS[@]}"; do
  for d in "/opt/ws/install/${pkg}/include/${pkg}" "/opt/ros/humble/include/${pkg}" ; do
    [[ -d "$d" ]] && INCLUDES+=("-I$d")
  done
done

g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror \
    ident_runner.cpp \
    "$OBJDIR/vessel_dynamics_model.cpp.o" \
    "$OBJDIR/capability_manifest.cpp.o" \
    "${INCLUDES[@]}" \
    -lyaml-cpp -lm \
    -o ident_runner

# Confirm the two object members really came out of the archive (fail fast if
# the install layout changes and the names drift).
if [[ ! -s "$OBJDIR/vessel_dynamics_model.cpp.o" || \
      ! -s "$OBJDIR/capability_manifest.cpp.o" ]]; then
  echo "IDENT FAIL: expected .o members missing after 'ar x'; archive members:" >&2
  ar t "${M5_INSTALL}/lib/libm5_shared_lib.a" | grep -E 'vessel_dynamics|capability_manifest' >&2 || true
  exit 1
fi

# ----------------------------------------------------- [2/4] VDM zigzag sim
echo "=== [2/4] VDM zigzag simulation -> zigzag.csv ==="
( cd "$WORKTREE_ROOT" && "$OLDPWD/ident_runner" "$FIXTURE" ) > zigzag.csv
echo "zigzag.csv: $(wc -l < zigzag.csv) lines"

# ------------------------------------------------------- [3/4] c(u) fit
echo "=== [3/4] c(u) yaw-gain + integrator model-class fit ==="
python3 ident_nomoto.py zigzag.csv nomoto_params.json
echo "--- nomoto_params.json ---"
cat nomoto_params.json

# --------------------------------------------- [4/4] verify (forward-match)
echo "=== [4/4] verification (double-integrator forward-match + linearity gate) ==="
python3 verify_nomoto.py nomoto_params.json zigzag.csv
