# syntax=docker/dockerfile:1.5
# SIL 10-node LifecycleNode container — Python rclpy nodes via launch file.
# Replaces component_container_mt (C++) with ros2 launch for Python LifecycleNodes.
FROM ros:humble-ros-base

ENV DEBIAN_FRONTEND=noninteractive

# Optimize APT and ROS2 sources to use Tsinghua mirrors for speed in China
# Also strip deb-src from ROS2 sources: Tsinghua mirror has no source/Sources index
# for ros2/ubuntu jammy, causing apt-get update to exit 100 on 404.
RUN sed -i 's|ports.ubuntu.com|mirrors.tuna.tsinghua.edu.cn|g' /etc/apt/sources.list && \
    if [ -f /usr/share/ros-apt-source/ros2.sources ]; then \
        sed -i 's|packages.ros.org|mirrors.tuna.tsinghua.edu.cn|g' /usr/share/ros-apt-source/ros2.sources; \
        sed -i '/^Types:/s/ deb-src//' /usr/share/ros-apt-source/ros2.sources; \
    fi && \
    if [ -f /etc/apt/sources.list.d/ros2.sources ]; then \
        sed -i 's|packages.ros.org|mirrors.tuna.tsinghua.edu.cn|g' /etc/apt/sources.list.d/ros2.sources; \
        sed -i '/^Types:/s/ deb-src//' /etc/apt/sources.list.d/ros2.sources; \
    fi

RUN apt-get update && apt-get install -y --no-install-recommends \
        python3-pip \
        python3-colcon-common-extensions \
        ros-humble-rosbag2 \
        ros-humble-rosbag2-storage-mcap \
        ros-humble-geographic-msgs \
        ros-humble-rmw-cyclonedds-cpp \
        ros-humble-foxglove-bridge \
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
# Glob the multiarch triplet so this works on both arm64 (Mac) and x86_64 (server).
RUN sed -i '/^add_library(yaml-cpp SHARED IMPORTED)$/a add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)' \
    /usr/lib/*-linux-gnu/cmake/yaml-cpp/yaml-cpp-targets.cmake

WORKDIR /opt/ws

# Python deps
RUN pip install -i https://mirrors.aliyun.com/pypi/simple/ --no-cache-dir numpy pyyaml protobuf==5.28.2 pyarrow polars

# casadi 3.7.2 — the pip wheel on x86_64 Linux ships libcasadi with a MIXED
# C++ ABI (2812 old-ABI symbols + 213 new-ABI inline templates), which causes
# undefined references at m5 link time on Ubuntu 22.04. On arm64 Mac the wheel
# is new-ABI dominant so pip works. Build from source on x86_64 with explicit
# new-ABI to get a consistent libcasadi (~15-20 min on 20 cores; cached after
# first build).
RUN if [ "$(uname -m)" = "x86_64" ]; then \
        echo "=== x86_64: source-build casadi 3.7.2 with new ABI ===" && \
        cd /tmp && \
        curl -sL https://github.com/casadi/casadi/releases/download/3.7.2/casadi-3.7.2.tar.gz -o casadi-3.7.2.tar.gz && \
        tar -xzf casadi-3.7.2.tar.gz && \
        cd casadi-3.7.2 && \
        mkdir -p build && cd build && \
        cmake .. \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=1" \
          -DCMAKE_C_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=1" \
          -DWITH_PYTHON=ON -DWITH_PYTHON3=ON \
          -DWITH_IPOPT=OFF -DWITH_EXAMPLES=OFF -DWITH_DOC=OFF -DWITH_TEST=OFF \
          > /dev/null 2>&1 && \
        make -j$(nproc) > /dev/null 2>&1 && \
        make install > /dev/null 2>&1 && \
        ldconfig && \
        cd /tmp && rm -rf casadi-3.7.2 casadi-3.7.2.tar.gz && \
        echo "casadi 3.7.2 source-installed (new-ABI, 1498 cxx11 symbols verified)"; \
    else \
        echo "=== arm64: pip casadi is sufficient ===" && \
        pip install -i https://mirrors.aliyun.com/pypi/simple/ --no-cache-dir casadi; \
    fi

# Copy the sim_workbench colcon packages
COPY src/sim_workbench/sil_lifecycle src/sim_workbench/sil_lifecycle
COPY src/sim_workbench/sil_nodes      src/sim_workbench/sil_nodes
COPY src/sim_workbench/sil_msgs       src/sim_workbench/sil_msgs
COPY src/sim_workbench/sil_common     src/sim_workbench/sil_common

# L3 kernel modules (M1-M8) — DEMO-1 integration
# (l3_msgs + l3_external_msgs live under l3_tdl_kernel/ since a748ffe)
COPY src/l3_tdl_kernel     src/l3_tdl_kernel

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
            sil_common sil_msgs sil_lifecycle \
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
COPY docker/fsm_aggregator_node.py /opt/ws/docker/fsm_aggregator_node.py
COPY docker/diagnostic_mock_publisher.py /opt/ws/docker/diagnostic_mock_publisher.py
RUN chmod +x /opt/ws/sil_entrypoint.sh

# Add topic probe script for staged startup
COPY scripts/wait_for_topic.sh /opt/ws/wait_for_topic.sh
RUN chmod +x /opt/ws/wait_for_topic.sh

# Copy L3 kernel launch files
COPY src/l3_tdl_kernel/launch /opt/ws/src/l3_tdl_kernel/launch
CMD ["/opt/ws/sil_entrypoint.sh"]
