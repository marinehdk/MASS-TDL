# docker/ci.Dockerfile — Local CI base image for SIL services.
# Build: docker build -t mass-l3/ci:humble-ubuntu22.04 -f docker/ci.Dockerfile .

# Stage 1: Base ROS2 Humble + system deps
FROM ros:humble-ros-base AS base

ENV DEBIAN_FRONTEND=noninteractive

# Install ROS2 packages + build tools + Python
RUN apt-get update && apt-get install -y --no-install-recommends \
    ros-humble-rclcpp-components \
    ros-humble-foxglove-bridge \
    ros-humble-rosbag2-storage-mcap \
    python3-pip \
    python3-colcon-common-extensions \
    python3-pytest \
    && rm -rf /var/lib/apt/lists/*

# Pre-create common directories
RUN mkdir -p /opt/ws /var/sil/scenarios /var/sil/runs /var/sil/exports

# Source ROS2 in bashrc for convenience
RUN echo 'source /opt/ros/humble/setup.bash' >> /root/.bashrc

WORKDIR /opt/ws

# Verify ROS2 is functional (Humble: ros2 --version not available, use env check)
RUN /bin/bash -c "source /opt/ros/humble/setup.bash && echo ROS_DISTRO=$ROS_DISTRO && ros2 -h > /dev/null && echo 'ROS2 OK'"
