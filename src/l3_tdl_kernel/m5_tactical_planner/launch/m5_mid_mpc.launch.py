import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config_dir = os.path.join(
        get_package_share_directory("m5_tactical_planner"), "config")
    return LaunchDescription([
        Node(
            package="m5_tactical_planner",
            executable="m5_mid_mpc_node",
            name="mid_mpc",
            output="screen",
            remappings=[
                ("/m6/colregs_constraint", "/l3/m6/colregs_constraint"),
            ],
            parameters=[os.path.join(config_dir, "m5_params.yaml")],
        ),
        # P6: BC-MPC activation — event-driven collision-avoidance override.
        # Launched alongside Mid-MPC from the same yaml for shared config.
        Node(
            package="m5_tactical_planner",
            executable="m5_bc_mpc_node",
            name="bc_mpc",
            output="screen",
            parameters=[os.path.join(config_dir, "m5_params.yaml")],
        ),
    ])
