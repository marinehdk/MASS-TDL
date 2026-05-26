# syntax=docker/dockerfile:1.5
# SIL 10-node LifecycleNode container — Python rclpy nodes via launch file.
# Replaces component_container_mt (C++) with ros2 launch for Python LifecycleNodes.
FROM ros:humble-ros-base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        python3-pip \
        python3-colcon-common-extensions \
        ros-humble-rosbag2 \
        ros-humble-rosbag2-storage-mcap \
        ros-humble-geographic-msgs \
        libeigen3-dev \
        libyaml-cpp-dev \
        libspdlog-dev \
        libgeographic-dev \
        libboost-dev \
        nlohmann-json3-dev \
        ccache \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 22.04 libyaml-cpp-dev 0.7.0 exports "yaml-cpp" (no namespace).
# Upstream CMakeLists.txt (M1-M6, M8) expects "yaml-cpp::yaml-cpp".
# Patch the targets file to add the namespace alias.
RUN sed -i '/^add_library(yaml-cpp SHARED IMPORTED)$/a add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)' \
    /usr/lib/aarch64-linux-gnu/cmake/yaml-cpp/yaml-cpp-targets.cmake

WORKDIR /opt/ws

# Copy the sim_workbench colcon packages
COPY src/sim_workbench/sil_lifecycle src/sim_workbench/sil_lifecycle
COPY src/sim_workbench/sil_nodes      src/sim_workbench/sil_nodes
COPY src/sim_workbench/sil_msgs       src/sim_workbench/sil_msgs

# L3 kernel modules (M1-M8) — DEMO-1 integration
# (l3_msgs + l3_external_msgs live under l3_tdl_kernel/ since a748ffe)
COPY src/l3_tdl_kernel     src/l3_tdl_kernel

# Python deps
RUN pip install --no-cache-dir numpy pyyaml protobuf==5.28.2 pyarrow polars
RUN pip install --no-cache-dir casadi

# Build the workspace.
# --mount=type=cache keeps intermediate build artifacts (ccache compiler cache)
# across `docker build` runs — only changed packages recompile.
RUN --mount=type=cache,target=/root/.ccache,sharing=shared \
    . /opt/ros/humble/setup.sh && \
    colcon build --symlink-install \
        --parallel-workers 2 \
        --cmake-args \
            -DBUILD_TESTING=OFF \
            -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake \
            -DCMAKE_C_COMPILER_LAUNCHER=ccache \
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        --packages-select \
            l3_msgs l3_external_msgs \
            sil_msgs sil_lifecycle \
            ship_dynamics env_disturbance target_vessel \
            sensor_mock tracker_mock scenario_authoring \
            fault_injection scoring \
            m1_odd_envelope_manager \
            m2_world_model \
            m3_mission_manager \
            m4_behavior_arbiter \
            m5_tactical_planner \
            m6_colregs_reasoner \
            m7_safety_supervisor \
            m8_hmi_transparency_bridge

RUN echo 'source /opt/ws/install/setup.bash' >> /root/.bashrc

# Launch all 10 Python LifecycleNodes directly (ament_python packages
# have no libexec dir; ros2 launch fails. Use ros2 run + lifecycle cli instead.)
COPY docker/sil_entrypoint.sh /opt/ws/sil_entrypoint.sh
COPY docker/sil_topic_bridge.py /opt/ws/docker/sil_topic_bridge.py
COPY docker/mock_l2_publisher.py /opt/ws/docker/mock_l2_publisher.py
RUN chmod +x /opt/ws/sil_entrypoint.sh

# Add topic probe script for staged startup
COPY scripts/wait_for_topic.sh /opt/ws/wait_for_topic.sh
RUN chmod +x /opt/ws/wait_for_topic.sh

# Copy L3 kernel launch files
COPY src/l3_tdl_kernel/launch /opt/ws/src/l3_tdl_kernel/launch
CMD ["/opt/ws/sil_entrypoint.sh"]
