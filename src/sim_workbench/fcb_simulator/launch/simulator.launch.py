# SPDX-License-Identifier: Proprietary
"""Launch fcb_simulator and ais_bridge for D1.3a SIL integration."""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, LogInfo
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    replay_rate_arg = DeclareLaunchArgument(
        'replay_rate_x',
        default_value='1.0',
        description='AIS replay speed multiplier: 1=realtime, 5=5x, 10=10x'
    )
    dataset_path_arg = DeclareLaunchArgument(
        'dataset_path',
        default_value='data/ais_datasets/AIS_synthetic_1h.csv',
        description='Path to NOAA CSV or DMA NMEA AIS dataset file'
    )
    dataset_format_arg = DeclareLaunchArgument(
        'dataset_format',
        default_value='noaa_csv',
        description='Dataset format: noaa_csv or dma_nmea'
    )

    fcb_config = PathJoinSubstitution([
        FindPackageShare('fcb_simulator'), 'config', 'fcb_dynamics.yaml'
    ])

    fcb_node = Node(
        package='fcb_simulator',
        executable='fcb_simulator_node',
        name='fcb_simulator',
        parameters=[fcb_config],
        output='screen',
        emulate_tty=True,
    )

    ais_node = Node(
        package='ais_bridge',
        executable='ais_replay_node',
        name='ais_replay',
        parameters=[{
            'dataset_path':    LaunchConfiguration('dataset_path'),
            'dataset_format':  LaunchConfiguration('dataset_format'),
            'replay_rate_x':   LaunchConfiguration('replay_rate_x'),
            'publish_rate_hz': 2.0,
            'max_targets':     50,
        }],
        output='screen',
        emulate_tty=True,
    )

    return LaunchDescription([
        replay_rate_arg,
        dataset_path_arg,
        dataset_format_arg,
        LogInfo(msg='D1.3a launch: fcb_simulator + ais_bridge'),
        fcb_node,
        ais_node,
    ])
