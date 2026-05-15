# SIL 10-node LifecycleNode container — Python rclpy nodes via launch file.
# Replaces component_container_mt (C++) with ros2 launch for Python LifecycleNodes.
FROM ros:humble-ros-base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        python3-pip \
        python3-colcon-common-extensions \
        ros-humble-rosbag2 \
        ros-humble-rosbag2-storage-mcap \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/ws

# Copy the sim_workbench colcon packages
COPY src/sim_workbench/sil_lifecycle src/sim_workbench/sil_lifecycle
COPY src/sim_workbench/sil_nodes      src/sim_workbench/sil_nodes
COPY src/sim_workbench/sil_msgs       src/sim_workbench/sil_msgs

# Python deps
RUN pip install --no-cache-dir numpy pyyaml protobuf==5.28.2 pyarrow polars

# Build the workspace
RUN . /opt/ros/humble/setup.sh && \
    colcon build --symlink-install \
        --packages-select sil_msgs sil_lifecycle \
            ship_dynamics env_disturbance target_vessel \
            sensor_mock tracker_mock scenario_authoring \
            fault_injection scoring

RUN echo 'source /opt/ws/install/setup.bash' >> /root/.bashrc

# Launch all 10 Python LifecycleNodes directly (ament_python packages
# have no libexec dir; ros2 launch fails. Use ros2 run + lifecycle cli instead.)
COPY docker/sil_entrypoint.sh /opt/ws/sil_entrypoint.sh
RUN chmod +x /opt/ws/sil_entrypoint.sh
CMD ["/opt/ws/sil_entrypoint.sh"]
