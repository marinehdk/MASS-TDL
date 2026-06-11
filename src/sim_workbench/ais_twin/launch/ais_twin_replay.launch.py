from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription(
        [
            Node(
                package="ais_twin",
                executable="ais_twin_replay_node",
                name="ais_twin_replay_node",
                output="screen",
            )
        ]
    )
