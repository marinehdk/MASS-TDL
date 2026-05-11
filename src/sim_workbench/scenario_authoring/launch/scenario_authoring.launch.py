"""scenario_authoring.launch.py — Config-driven L1 mode switching.

Mode selection:
  scenario_yaml not set              -> sil_mock_publisher (synthetic, D1.3b.1)
  scenario_yaml + ais_derived source -> ais_replay_node      (AIS, D1.3b.2)
  scenario_yaml + other source       -> sil_mock_publisher   (Imazu/synthetic)

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

        # synthetic mode: sil_mock_publisher (D1.3b.1 existing)
        Node(
            package="sil_mock_publisher",
            executable="sil_mock_node",
            name="sil_mock_publisher",
            output="screen",
            condition=IfCondition(PythonExpression([
                "not bool('", scenario_yaml, "') or not ", is_ais_derived
            ])),
        ),

        # ais_replay mode: scenario_authoring replay node (D1.3b.2 new)
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
            " else 'synthetic (D1.3b.1)'"
        ])]),
    ])
