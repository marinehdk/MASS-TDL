# SIL component container — runs the 9 ROS2 nodes inside component_container_mt.
# Spec: docs/Design/SIL/2026-05-12-sil-architecture-design.md §2 (responsibility table)
FROM ros:humble-ros-base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        python3-pip \
        python3-colcon-common-extensions \
        ros-humble-rclcpp-components \
        ros-humble-rosbag2 \
        ros-humble-rosbag2-storage-mcap \
        ros-humble-foxglove-bridge \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/ws

# Copy the sim_workbench colcon packages (sil_lifecycle + sil_nodes + sil_msgs)
COPY src/sim_workbench/sil_lifecycle src/sim_workbench/sil_lifecycle
COPY src/sim_workbench/sil_nodes      src/sim_workbench/sil_nodes
COPY src/sim_workbench/sil_msgs       src/sim_workbench/sil_msgs

# Python deps used by the Python ROS2 nodes
RUN pip install --no-cache-dir numpy pyyaml protobuf==5.28.2

# Build the workspace
RUN . /opt/ros/humble/setup.sh && \
    colcon build --symlink-install --merge-install \
        --packages-select sil_msgs sil_lifecycle \
            ship_dynamics env_disturbance target_vessel \
            sensor_mock tracker_mock scenario_authoring \
            fault_injection scoring

# Ensure the workspace is sourced for any interactive shell
RUN echo 'source /opt/ws/install/setup.bash' >> /root/.bashrc

# Default command runs component_container_mt; compose overrides if needed
CMD ["/bin/bash", "-lc", \
     "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
      ros2 run rclcpp_components component_container_mt --ros-args -r __node:=sil_container"]
