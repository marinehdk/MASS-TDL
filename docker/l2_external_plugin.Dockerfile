# syntax=docker/dockerfile:1.5
FROM ros:humble-ros-base

ENV DEBIAN_FRONTEND=noninteractive
ENV RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ENV PYTHONPATH=/opt/l2_adapters

RUN apt-get update && apt-get install -y --no-install-recommends \
        ccache \
        python3-colcon-common-extensions \
        python3-yaml \
        ros-humble-rmw-cyclonedds-cpp \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/l2_ws

COPY plugins/l2_external/ros2_ws/src/route_planning_ros2 src/route_planning_ros2
COPY plugins/l2_external/ros2_ws/src/platform/ship_interfaces src/ship_interfaces
COPY src/sim_workbench/sil_msgs src/sil_msgs

RUN --mount=type=cache,target=/root/.ccache,sharing=shared \
    . /opt/ros/humble/setup.sh && \
    colcon build --symlink-install \
        --cmake-args \
            -DBUILD_TESTING=OFF \
            -DCMAKE_C_COMPILER_LAUNCHER=ccache \
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        --packages-select ship_interfaces sil_msgs route_planning_ros2

COPY src/sim_workbench/external_adapters/external_adapters /opt/l2_adapters/external_adapters
COPY plugins/l2_external/entrypoint.sh /opt/l2_entrypoint.sh
RUN chmod +x /opt/l2_entrypoint.sh

CMD ["/opt/l2_entrypoint.sh"]
