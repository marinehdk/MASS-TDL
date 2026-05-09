import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config_dir = os.path.join(
        get_package_share_directory("m6_colregs_reasoner"), "config"
    )
    return LaunchDescription([
        Node(
            package="m6_colregs_reasoner",
            executable="m6_colregs_reasoner",
            name="m6_colregs_reasoner",
            output="screen",
            parameters=[
                os.path.join(config_dir, "m6_params.yaml"),
                os.path.join(config_dir, "odd_aware_thresholds.yaml"),
                {"config_dir": config_dir},
            ],
        ),
    ])
