"""SIL mock publisher launch file (D1.3b through D2.4).

D2.5 migration: comment out SilMockNode, uncomment M1/M2 real nodes.
Topic names /l3/sat/data and /l3/m1/odd_state remain unchanged.
"""
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    return LaunchDescription([
        Node(
            package="sil_mock_publisher",
            executable="sil_mock_node",
            name="sil_mock_publisher",
            output="screen",
        ),
        # D2.5: replace with real M1/M2 nodes:
        # Node(
        #     package="m1_odd_envelope_manager",
        #     executable="odd_envelope_manager_node",
        #     name="m1_odd_envelope_manager",
        #     output="screen",
        # ),
        # Node(
        #     package="m2_world_model",
        #     executable="world_model_node",
        #     name="m2_world_model",
        #     output="screen",
        # ),
    ])
