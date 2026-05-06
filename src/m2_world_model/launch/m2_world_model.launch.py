import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config_dir = os.path.join(
        get_package_share_directory("m2_world_model"), "config"
    )
    return LaunchDescription([
        Node(
            package="m2_world_model",
            executable="m2_world_model",
            name="m2_world_model",
            output="screen",
            parameters=[os.path.join(config_dir, "m2_params.yaml")],
        ),
    ])
