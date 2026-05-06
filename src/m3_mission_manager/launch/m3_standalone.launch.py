# Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
#
# SPDX-License-Identifier: Proprietary
#
# M3 Mission Manager — standalone launch file.
# Per v1.1.2 §7 + §3.3 Node Topology.

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    config_dir = os.path.join(
        get_package_share_directory("m3_mission_manager"), "config"
    )
    return LaunchDescription([
        Node(
            package="m3_mission_manager",
            executable="m3_mission_manager",
            name="m3_mission_manager",
            output="screen",
            parameters=[os.path.join(config_dir, "m3_params.yaml")],
        ),
    ])
