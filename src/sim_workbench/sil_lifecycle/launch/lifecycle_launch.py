"""Launch file stub -- Phase 2: actual ROS2 launch."""
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        Node(
            package='sil_lifecycle',
            executable='scenario_lifecycle_mgr',
            name='scenario_lifecycle_mgr',
            output='screen',
        ),
    ])
