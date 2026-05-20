# SIL 10-node LifecycleNode container — Python rclpy nodes via launch file.
# Replaces component_container_mt (C++) with ros2 launch for Python LifecycleNodes.
FROM ros:humble-ros-base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        python3-pip \
        python3-colcon-common-extensions \
        ros-humble-rosbag2 \
        ros-humble-rosbag2-storage-mcap \
        libeigen3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/ws

# Copy the sim_workbench colcon packages
COPY src/sim_workbench/sil_lifecycle src/sim_workbench/sil_lifecycle
COPY src/sim_workbench/sil_nodes      src/sim_workbench/sil_nodes
COPY src/sim_workbench/sil_msgs       src/sim_workbench/sil_msgs

# L3 kernel modules (M1-M8) — DEMO-1 integration
COPY src/l3_msgs           src/l3_msgs
COPY src/l3_external_msgs  src/l3_external_msgs
COPY src/l3_tdl_kernel     src/l3_tdl_kernel

# Python deps
RUN pip install --no-cache-dir numpy pyyaml protobuf==5.28.2 pyarrow polars
RUN pip install --no-cache-dir python3-casadi || echo "WARNING: CasADi not available — M5 will use analytical stub"

# Build the workspace
RUN . /opt/ros/humble/setup.sh && \
    colcon build --symlink-install \
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
RUN chmod +x /opt/ws/sil_entrypoint.sh

# Add topic probe script for staged startup
COPY scripts/wait_for_topic.sh /opt/ws/wait_for_topic.sh
RUN chmod +x /opt/ws/wait_for_topic.sh

# Copy L3 kernel launch files
COPY src/l3_tdl_kernel/launch /opt/ws/src/l3_tdl_kernel/launch
CMD ["/opt/ws/sil_entrypoint.sh"]
