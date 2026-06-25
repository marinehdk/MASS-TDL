from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    use_sim_time = LaunchConfiguration('use_sim_time', default='false')
    time_scale = LaunchConfiguration('time_scale', default='10.0')
    dynamics_rate = LaunchConfiguration('dynamics_rate', default='500.0')
    guidance_period = LaunchConfiguration('guidance_period', default='0.05')
    control_period = LaunchConfiguration('control_period', default='0.01')
    allocation_update_rate = LaunchConfiguration('allocation_update_rate', default='10.0')

    ship_config_path = PathJoinSubstitution([
        FindPackageShare('ship_bringup'), 'config', 'ship_config_fast10.yaml'
    ])
    propulsion_policy_config_path = PathJoinSubstitution([
        FindPackageShare('mission_supervisor'), 'config', 'propulsion_policy.yaml'
    ])

    common = [{'use_sim_time': use_sim_time}, ship_config_path]

    return LaunchDescription([
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        DeclareLaunchArgument('time_scale', default_value='10.0', description='Fast test time multiplier; keep 1.0 for normal physics timing.'),
        DeclareLaunchArgument('dynamics_rate', default_value='500.0', description='Wall-clock dynamics loop rate for fast test.'),
        DeclareLaunchArgument('guidance_period', default_value='0.05', description='Wall-clock guidance loop period for 10x fast test.'),
        DeclareLaunchArgument('control_period', default_value='0.01', description='Wall-clock controller period for 10x fast test.'),
        DeclareLaunchArgument('allocation_update_rate', default_value='10.0', description='Allocator effective dt rate for fast test actuator limits.'),

        Node(
            package='ship_dynamics', executable='ship_dynamics_node', name='ship_dynamics_node', output='screen',
            parameters=common + [
                {'initial_position.x': 0.0}, {'initial_position.y': 0.0}, {'initial_position.yaw': 0.0},
                {'auto_initial_yaw_from_route': True},
            ]),
        Node(
            package='ship_guidance', executable='active_route_manager_node', name='active_route_manager_node', output='screen',
            parameters=common),
        Node(
            package='ship_guidance', executable='coordinate_transform_node', name='coordinate_transform_node', output='screen',
            parameters=common + [
                {'enable_route_update_guard': True},
                {'min_route_update_interval_s': 10.0},
                {'min_future_update_distance_m': 500.0},
                {'max_dynamic_lateral_delta_m': 100.0},
                {'reject_reverse_segments': True},
            ]),
        Node(
            package='ship_guidance', executable='ship_guidance_node', name='ship_guidance_node', output='screen',
            parameters=common + [
                {'use_adaptive_los': True},
                {'wait_for_route_plan': True},
            ]),
        Node(
            package='mission_supervisor', executable='propulsion_policy_node', name='propulsion_policy_node', output='screen',
            parameters=[{'use_sim_time': use_sim_time}, {'config_file': propulsion_policy_config_path}, {'shadow_mode': True}]),
        Node(
            package='thrust_allocation', executable='thrust_allocation_node', name='thrust_allocation_node', output='screen',
            parameters=common + [
                {'env_feedforward_weight': 0.0},
            ]),
        Node(
            package='ship_control', executable='ship_control_node', name='ship_control_node', output='screen',
            parameters=common + [
                {'ndo.enable': False},
            ]),
    ])
