import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    config_dir = os.path.join(
        get_package_share_directory("m1_odd_envelope_manager"), "config"
    )
    return LaunchDescription([
        Node(
            package="m1_odd_envelope_manager",
            executable="m1_odd_envelope_manager",
            name="m1_odd_envelope_manager",
            output="screen",
            parameters=[
                os.path.join(config_dir, "m1_params.yaml"),
                {"yaml_path": os.path.join(config_dir, "m1_params.yaml")},
            ],
        ),
    ])
