import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    pkg_dir = get_package_share_directory('m4_behavior_arbiter')

    config_path = os.path.join(pkg_dir, 'config', 'm4_params.yaml')

    return LaunchDescription([
        Node(
            package='m4_behavior_arbiter',
            executable='m4_behavior_arbiter',
            name='behavior_arbiter',
            parameters=[
                config_path,
                {'config_dir': os.path.join(pkg_dir, 'config')},
            ],
        )
    ])
