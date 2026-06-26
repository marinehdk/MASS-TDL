"""
sim_with_route_launch.py
========================
整合启动: 同事的完整GNC节点 + 你的桥接节点
基于同事的 ship_bringup/launch/sim_launch.py, 增加 gnc_sim_node
"""

from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    ship_config_path = PathJoinSubstitution([
        FindPackageShare('ship_bringup'),
        'config',
        'ship_config.yaml'
    ])

    # 读取 URDF
    ship_description_share = get_package_share_directory('ship_description')
    ship_urdf_path = os.path.join(ship_description_share, 'urdf', 'ship.urdf')
    with open(ship_urdf_path, 'r') as f:
        robot_description_content = f.read()

    # ── 1. 同事的C++节点: ship_dynamics (4-DOF动力学) ──
    ship_dynamics_node = Node(
        package='ship_dynamics',
        executable='ship_dynamics_node',
        name='ship_dynamics_node',
        output='screen',
        parameters=[
            {'use_sim_time': False},
            ship_config_path,
            {'initial_position.x': 0.0},
            {'initial_position.y': 0.0},
            {'initial_position.yaw': 0.0}
        ]
    )

    # ── 2. 同事的C++节点: coordinate_transform (WGS84→NED) ──
    coordinate_transform_node = Node(
        package='ship_guidance',
        executable='coordinate_transform_node',
        name='coordinate_transform_node',
        output='screen',
        parameters=[
            {'use_sim_time': False},
            ship_config_path
        ]
    )

    # ── 3. 同事的C++节点: ship_guidance (ILOS制导) ──
    ship_guidance_node = Node(
        package='ship_guidance',
        executable='ship_guidance_node',
        name='ship_guidance_node',
        output='screen',
        parameters=[
            {'use_sim_time': False},
            ship_config_path,
            {'use_adaptive_los': True}
        ]
    )

    # ── 4. 同事的C++节点: thrust_allocation (7推进器分配) ──
    thrust_allocation_node = Node(
        package='thrust_allocation',
        executable='thrust_allocation_node',
        name='thrust_allocation_node',
        output='screen',
        parameters=[
            {'use_sim_time': False},
            ship_config_path
        ]
    )

    # ── 5. 同事的C++节点: ship_control (PID+NDO) ──
    ship_control_node = Node(
        package='ship_control',
        executable='ship_control_node',
        name='ship_control_node',
        output='screen',
        parameters=[
            ship_config_path,
            {'use_sim_time': False},
            {'ndo.enable': False}  # 静水测试关闭NDO
        ]
    )

    # ── 6. robot_state_publisher (URDF) ──
    robot_state_publisher_node = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[{
            'use_sim_time': False,
            'robot_description': robot_description_content
        }]
    )

    # ── 7. 你的Python桥接节点: 发布航线 + 订阅Odometry ──
    gnc_sim_node = Node(
        package='route_planning_ros2',
        executable='gnc_sim_node',
        name='gnc_sim_node',
        output='screen',
        parameters=[{'use_sim_time': False}]
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'use_sim_time',
            default_value='false',
            description='Use simulation clock if true'
        ),
        ship_dynamics_node,
        coordinate_transform_node,
        ship_guidance_node,
        thrust_allocation_node,
        ship_control_node,
        robot_state_publisher_node,
        gnc_sim_node,
    ])
