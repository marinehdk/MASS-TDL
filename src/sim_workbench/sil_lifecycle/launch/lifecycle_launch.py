"""Launch LifecycleManagerNode as a lifecycle-managed node."""
from launch import LaunchDescription
from launch_ros.actions import LifecycleNode


def generate_launch_description():
    return LaunchDescription([
        LifecycleNode(
            package="sil_lifecycle",
            executable="scenario_lifecycle_mgr",
            name="scenario_lifecycle_mgr",
            output="screen",
        ),
    ])
