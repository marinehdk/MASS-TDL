from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    # parameters omitted: nodes use compiled-in defaults during integration tests;
    # per-module YAML config loading is added in Wave 4b (HIL full stack)
    nodes = [
        Node(
            package='m1_odd_envelope_manager',
            executable='m1_odd_envelope_manager',
            name='m1_odd_envelope_manager',
            output='screen',
        ),
        Node(
            package='m2_world_model',
            executable='m2_world_model',
            name='m2_world_model',
            output='screen',
        ),
        Node(
            package='m3_mission_manager',
            executable='m3_mission_manager',
            name='m3_mission_manager',
            output='screen',
        ),
        # M4 omitted: stub package with no compiled node yet (Wave 4a)
        # RFC-001 scheme B: M5 exposes two nodes (Mid-MPC + BC-MPC dual interface)
        Node(
            package='m5_tactical_planner',
            executable='m5_mid_mpc_node',
            name='m5_mid_mpc_node',
            output='screen',
        ),
        Node(
            package='m5_tactical_planner',
            executable='m5_bc_mpc_node',
            name='m5_bc_mpc_node',
            output='screen',
        ),
        Node(
            package='m6_colregs_reasoner',
            executable='m6_colregs_reasoner',
            name='m6_colregs_reasoner',
            output='screen',
        ),
        Node(
            package='m7_safety_supervisor',
            executable='m7_safety_supervisor',
            name='m7_safety_supervisor',
            output='screen',
        ),
        Node(
            package='m8_hmi_transparency_bridge',
            executable='m8_hmi_transparency_bridge_node',
            name='m8_hmi_transparency_bridge',
            output='screen',
        ),
        # External-boundary mock: publishes L2/Fusion/Checker/Reflex/Override stimulus topics
        Node(
            package='l3_external_mock_publisher',
            executable='external_mock_publisher',
            name='l3_external_mock_publisher',
            output='screen',
        ),
    ]
    return LaunchDescription(nodes)
