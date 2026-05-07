from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    # Executable names sourced from each package's CMakeLists.txt (Wave 4a).
    # M4 (m4_behavior_arbiter) is a stub package without a compiled node yet —
    # its entry is kept for completeness; it will be enabled once M4 Phase E1
    # implementation lands.
    # M5 exposes two nodes (Mid-MPC and BC-MPC) per RFC-001 scheme B.
    nodes = [
        Node(
            package='m1_odd_envelope_manager',
            executable='m1_odd_envelope_manager',
            name='m1_odd_envelope_manager',
        ),
        Node(
            package='m2_world_model',
            executable='m2_world_model',
            name='m2_world_model',
        ),
        Node(
            package='m3_mission_manager',
            executable='m3_mission_manager',
            name='m3_mission_manager',
        ),
        # M4 stub — uncomment once m4_behavior_arbiter has a compiled node:
        # Node(
        #     package='m4_behavior_arbiter',
        #     executable='m4_behavior_arbiter',
        #     name='m4_behavior_arbiter',
        # ),
        Node(
            package='m5_tactical_planner',
            executable='m5_mid_mpc_node',
            name='m5_mid_mpc',
        ),
        Node(
            package='m5_tactical_planner',
            executable='m5_bc_mpc_node',
            name='m5_bc_mpc',
        ),
        Node(
            package='m6_colregs_reasoner',
            executable='m6_colregs_reasoner',
            name='m6_colregs_reasoner',
        ),
        Node(
            package='m7_safety_supervisor',
            executable='m7_safety_supervisor',
            name='m7_safety_supervisor',
        ),
        Node(
            package='m8_hmi_transparency_bridge',
            executable='m8_hmi_transparency_bridge_node',
            name='m8_hmi_transparency_bridge',
        ),
        Node(
            package='l3_external_mock_publisher',
            executable='external_mock_publisher',
            name='l3_mock_publisher',
        ),
    ]
    return LaunchDescription(nodes)
