import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    scenario_path = LaunchConfiguration("scenario_path", default="")

    scenario_default = os.path.join(
        os.path.dirname(__file__),
        "../../../../scenarios/colregs/colreg-rule14-ho-001.yaml"
    )

    return LaunchDescription([
        DeclareLaunchArgument("scenario_path", default_value=scenario_default,
                              description="Path to scenario YAML file"),
        Node(
            package="l3_external_mock_publisher",
            executable="external_mock_publisher",
            name="l3_external_mock_publisher",
            output="screen",
            parameters=[{"scenario_path": scenario_path}],
        ),
    ])
