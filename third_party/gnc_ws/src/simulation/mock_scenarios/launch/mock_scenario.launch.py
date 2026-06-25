from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    scenario_name = LaunchConfiguration('scenario_name')
    scenario_file = LaunchConfiguration('scenario_file')
    loop = LaunchConfiguration('loop')

    default_scenario = PathJoinSubstitution([
        FindPackageShare('mock_scenarios'),
        'config',
        'scenarios',
        scenario_name,
    ])

    return LaunchDescription([
        DeclareLaunchArgument(
            'scenario_name',
            default_value='001_straight_calm.yaml',
            description='Scenario YAML file installed under mock_scenarios/config/scenarios.',
        ),
        DeclareLaunchArgument(
            'scenario_file',
            default_value=default_scenario,
            description='Absolute scenario YAML path. Overrides scenario_name when set by caller.',
        ),
        DeclareLaunchArgument(
            'loop',
            default_value='false',
            description='Loop scenario playback after duration_s.',
        ),
        Node(
            package='mock_scenarios',
            executable='mock_data_player',
            name='mock_data_player',
            output='screen',
            parameters=[
                {'scenario_file': scenario_file},
                {'loop': loop},
                {'publish_truth': True},
            ],
        ),
    ])
