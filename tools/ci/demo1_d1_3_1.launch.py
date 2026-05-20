#!/usr/bin/env python3
# tools/ci/demo1_d1_3_1.launch.py
"""DEMO-1 D1.3.1 集成 launch: ShipDynamicsNode + AISReplayNode。

启动后 30s 内两个 topic 均有数据:
  /sil/own_ship_state @ 50Hz
  /sil/target_vessel_state @ 2Hz (AIS 回放)
"""
from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration


def generate_launch_description():
    replay_rate = LaunchConfiguration('replay_rate_x', default='5')

    return LaunchDescription([
        DeclareLaunchArgument(
            'replay_rate_x', default_value='5',
            description='AIS replay speed multiplier (1=real-time, 5=5x, 10=10x)'
        ),

        # ShipDynamicsNode — FCB 4-DOF MMG @ 50Hz
        Node(
            package='ship_dynamics',
            executable='ship_dynamics_node',
            name='ship_dynamics_node',
            parameters=[{
                'vessel_class': 'FCB',
                'hull_class': 'SEMI_PLANING',
            }],
            output='screen',
        ),

        # AISReplayNode — NOAA/DMA AIS 历史回放
        Node(
            package='ais_bridge',
            executable='ais_replay_node',
            name='ais_replay_node',
            parameters=[{
                'dataset_path': 'data/ais_datasets/AIS_synthetic_1h.csv',
                'dataset_format': 'noaa_csv',
                'replay_rate_x': replay_rate,
                'publish_rate_hz': 2.0,
                'max_targets': 50,
            }],
            output='screen',
        ),
    ])
