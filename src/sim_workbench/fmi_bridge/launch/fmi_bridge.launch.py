# launch/fmi_bridge.launch.py
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description() -> LaunchDescription:
    return LaunchDescription([
        Node(
            package="fmi_bridge",
            executable="dds_fmu_node",
            name="dds_fmu_node",
            output="screen",
            parameters=[{"fmu_path": "fcb_mmg_4dof.fmu"}],
        ),
    ])
