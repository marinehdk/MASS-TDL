from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    default_config = str(
        Path(get_package_share_directory('safety_supervisor'))
        / 'config'
        / 'safety_limits.yaml'
    )

    return LaunchDescription([
        DeclareLaunchArgument('config_file', default_value=default_config),
        DeclareLaunchArgument('shadow_mode', default_value='true'),
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        DeclareLaunchArgument('phase', default_value='default'),
        DeclareLaunchArgument('publish_rate_hz', default_value='5.0'),
        Node(
            package='safety_supervisor',
            executable='safety_supervisor_node',
            name='safety_supervisor_node',
            output='screen',
            parameters=[
                {'config_file': LaunchConfiguration('config_file')},
                {'shadow_mode': LaunchConfiguration('shadow_mode')},
                {'use_sim_time': LaunchConfiguration('use_sim_time')},
                {'phase': LaunchConfiguration('phase')},
                {'publish_rate_hz': LaunchConfiguration('publish_rate_hz')},
            ],
        ),
    ])
