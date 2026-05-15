"""full_l3_stack_demo1.launch.py — DEMO-1 full L3 pipeline launch."""
import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, TimerAction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

def generate_launch_description() -> LaunchDescription:
    scenario_path = LaunchConfiguration("scenario_path", default="")

    external_mock = TimerAction(period=0.0, actions=[
        Node(
            package="l3_external_mock_publisher",
            executable="external_mock_publisher",
            name="l3_external_mock_publisher",
            output="screen",
            parameters=[{"scenario_path": scenario_path, "use_sim_time": True}],
        ),
    ])

    m1_node = TimerAction(period=1.0, actions=[
        Node(package="m1_odd_envelope_manager", executable="m1_odd_envelope_manager",
             name="m1_odd", output="screen",
             parameters=[{"use_sim_time": True}]),
    ])

    m2_node = TimerAction(period=1.0, actions=[
        Node(package="m2_world_model", executable="m2_world_model",
             name="m2_world", output="screen",
             parameters=[{"use_sim_time": True}]),
    ])

    m3_node = TimerAction(period=1.5, actions=[
        Node(package="m3_mission_manager", executable="m3_mission_manager",
             name="m3_mission", output="screen",
             parameters=[{"use_sim_time": True}]),
    ])

    m6_node = TimerAction(period=2.0, actions=[
        Node(package="m6_colregs_reasoner", executable="m6_colregs_reasoner",
             name="m6_colregs", output="screen",
             parameters=[{"use_sim_time": True}]),
    ])

    m4_node = TimerAction(period=2.5, actions=[
        Node(package="m4_behavior_arbiter", executable="m4_behavior_arbiter",
             name="m4_arbiter", output="screen",
             parameters=[{"use_sim_time": True}]),
    ])

    m5_node = TimerAction(period=3.0, actions=[
        Node(package="m5_tactical_planner", executable="mid_mpc_node",
             name="m5_mid_mpc", output="screen",
             remappings=[("/m6/colregs_constraint", "/l3/m6/colregs_constraint")],
             parameters=[{"use_sim_time": True}]),
    ])

    m7_node = TimerAction(period=3.5, actions=[
        Node(package="m7_safety_supervisor", executable="m7_safety_supervisor",
             name="m7_safety", output="screen",
             parameters=[{"use_sim_time": True}]),
    ])

    return LaunchDescription([
        DeclareLaunchArgument("scenario_path", default_value="",
                              description="Path to scenario YAML file"),
        external_mock, m1_node, m2_node, m3_node, m6_node, m4_node, m5_node, m7_node,
    ])
