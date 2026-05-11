"""Launch file for scenario_authoring pipeline."""
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        Node(
            package="scenario_authoring",
            executable="extract_encounters",
            name="extract_encounters",
            output="screen",
        ),
        Node(
            package="scenario_authoring",
            executable="ais_replay_node",
            name="ais_replay_node",
            output="screen",
        ),
    ])
