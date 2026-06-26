from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    default_config = str(
        Path(get_package_share_directory('mission_supervisor'))
        / 'config'
        / 'mission_gates.yaml'
    )
    default_captain_decision_config = str(
        Path(get_package_share_directory('mission_supervisor'))
        / 'config'
        / 'captain_decision_policy.yaml'
    )
    default_propulsion_policy_config = str(
        Path(get_package_share_directory('mission_supervisor'))
        / 'config'
        / 'propulsion_policy.yaml'
    )
    default_propulsion_compliance_config = str(
        Path(get_package_share_directory('mission_supervisor'))
        / 'config'
        / 'propulsion_policy_compliance.yaml'
    )

    return LaunchDescription([
        DeclareLaunchArgument('config_file', default_value=default_config),
        DeclareLaunchArgument('captain_decision_config_file', default_value=default_captain_decision_config),
        DeclareLaunchArgument('propulsion_policy_config_file', default_value=default_propulsion_policy_config),
        DeclareLaunchArgument('propulsion_compliance_config_file', default_value=default_propulsion_compliance_config),
        DeclareLaunchArgument('scenario_file', default_value=''),
        DeclareLaunchArgument('shadow_mode', default_value='true'),
        DeclareLaunchArgument('publish_rate_hz', default_value='2.0'),
        DeclareLaunchArgument('auto_advance', default_value='true'),
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        Node(
            package='mission_supervisor',
            executable='mission_supervisor_node',
            name='mission_supervisor_node',
            output='screen',
            parameters=[
                {'config_file': LaunchConfiguration('config_file')},
                {'scenario_file': LaunchConfiguration('scenario_file')},
                {'shadow_mode': LaunchConfiguration('shadow_mode')},
                {'publish_rate_hz': LaunchConfiguration('publish_rate_hz')},
                {'auto_advance': LaunchConfiguration('auto_advance')},
                {'use_sim_time': LaunchConfiguration('use_sim_time')},
            ],
        ),
        Node(
            package='mission_supervisor',
            executable='captain_decision_node',
            name='captain_decision_node',
            output='screen',
            parameters=[
                {'config_file': LaunchConfiguration('captain_decision_config_file')},
                {'scenario_file': LaunchConfiguration('scenario_file')},
                {'shadow_mode': LaunchConfiguration('shadow_mode')},
                {'publish_rate_hz': LaunchConfiguration('publish_rate_hz')},
                {'use_sim_time': LaunchConfiguration('use_sim_time')},
            ],
        ),
        Node(
            package='mission_supervisor',
            executable='propulsion_policy_node',
            name='propulsion_policy_node',
            output='screen',
            parameters=[
                {'config_file': LaunchConfiguration('propulsion_policy_config_file')},
                {'scenario_file': LaunchConfiguration('scenario_file')},
                {'shadow_mode': LaunchConfiguration('shadow_mode')},
                {'publish_rate_hz': LaunchConfiguration('publish_rate_hz')},
                {'use_sim_time': LaunchConfiguration('use_sim_time')},
            ],
        ),
        Node(
            package='mission_supervisor',
            executable='propulsion_policy_compliance_node',
            name='propulsion_policy_compliance_node',
            output='screen',
            parameters=[
                {'config_file': LaunchConfiguration('propulsion_compliance_config_file')},
                {'scenario_file': LaunchConfiguration('scenario_file')},
                {'shadow_mode': LaunchConfiguration('shadow_mode')},
                {'publish_rate_hz': LaunchConfiguration('publish_rate_hz')},
                {'use_sim_time': LaunchConfiguration('use_sim_time')},
            ],
        ),
    ])
