"""scenario_authoring.launch.py — Config-driven L1 mode switching.

Mode selection:
  scenario_yaml not set              -> sensor_mock + tracker_mock (synthetic)
  scenario_yaml + ais_derived source -> ais_replay_node      (AIS, D1.3b.2)
  scenario_yaml + other source       -> sensor_mock + tracker_mock (Imazu/synthetic)

Mock publishers removed — data flows through real SIL pipeline nodes.
rosbag mode: stub, deferred to D2.5.
"""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, LogInfo
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PythonExpression
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    scenario_yaml = LaunchConfiguration("scenario_yaml", default="")

    is_ais_derived = PythonExpression([
        "'ais_derived' in open('", scenario_yaml, "').read() if '", scenario_yaml, "' else ''"
    ])

    return LaunchDescription([
        DeclareLaunchArgument(
            "scenario_yaml",
            default_value="",
            description="Path to maritime-schema scenario YAML. Empty = synthetic mode.",
        ),

        Node(
            package="sensor_mock",
            executable="sensor_mock_node",
            name="sensor_mock_node",
            output="screen",
            condition=IfCondition(PythonExpression([
                "not bool('", scenario_yaml, "') or not ", is_ais_derived
            ])),
        ),

        Node(
            package="tracker_mock",
            executable="tracker_mock_node",
            name="tracker_mock_node",
            output="screen",
            condition=IfCondition(PythonExpression([
                "not bool('", scenario_yaml, "') or not ", is_ais_derived
            ])),
        ),

        Node(
            package="scenario_authoring",
            executable="ais_replay_node",
            name="ais_replay_node",
            output="screen",
            parameters=[{"yaml_path": scenario_yaml}],
            condition=IfCondition(is_ais_derived),
        ),

        LogInfo(msg=["L1 mode: ", PythonExpression([
            "'ais_replay (D1.3b.2)' if ", is_ais_derived,
            " else 'synthetic (sensor_mock + tracker_mock)'"
        ])]),
    ])
