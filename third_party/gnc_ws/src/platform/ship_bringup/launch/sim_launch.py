from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument, ExecuteProcess
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution, TextSubstitution, PythonExpression
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare
from ament_index_python.packages import get_package_share_directory
import os

def generate_launch_description():
    use_sim_time = LaunchConfiguration('use_sim_time', default='false')
    enable_wind = LaunchConfiguration('enable_wind', default='false')
    enable_current = LaunchConfiguration('enable_current', default='false')
    wind_u10 = LaunchConfiguration('wind_u10', default='0.0')
    wind_direction = LaunchConfiguration('wind_direction', default='0.0')
    wind_direction_is_from = LaunchConfiguration('wind_direction_is_from', default='true')
    current_speed = LaunchConfiguration('current_speed', default='0.0')
    current_direction = LaunchConfiguration('current_direction', default='0.0')
    current_direction_is_from = LaunchConfiguration('current_direction_is_from', default='true')
    env_feedforward_weight = LaunchConfiguration('env_feedforward_weight', default='0.0')
    enable_environment = PythonExpression(["'", enable_wind, "' == 'true' or '", enable_current, "' == 'true'"])

    ship_description_share = get_package_share_directory('ship_description')
    ship_urdf_path = os.path.join(ship_description_share, 'urdf', 'ship.urdf')
    with open(ship_urdf_path, 'r') as f:
        robot_description_content = f.read()

    ship_config_path = PathJoinSubstitution([
        FindPackageShare('ship_bringup'),
        'config',
        'ship_config.yaml'
    ])

    propulsion_policy_config_path = PathJoinSubstitution([
        FindPackageShare('mission_supervisor'),
        'config',
        'propulsion_policy.yaml'
    ])

    ship_urdf_path = PathJoinSubstitution([
        FindPackageShare('ship_description'),
        'urdf',
        'ship.urdf'
    ])

    current_coeffs_csv_path = PathJoinSubstitution([
        FindPackageShare('env_engines'),
        'data',
        'current_coeffs.csv'
    ])

    # === 环境载荷测试 ===
    wind_engine_node = Node(
        package='env_engines',
        executable='wind_engine_node',
        name='wind_engine_node',
        output='screen',
        condition=IfCondition(enable_wind),
        parameters=[
            ship_config_path,
            {'use_sim_time': use_sim_time},
            {'u10': ParameterValue(wind_u10, value_type=float)},
            {'wind_direction': ParameterValue(wind_direction, value_type=float)},
            {'wind_direction_is_from': ParameterValue(wind_direction_is_from, value_type=bool)}
        ]
    )
    #
    # wave_engine_node = Node(
    #     package='env_engines',
    #     executable='wave_engine_node',
    #     name='wave_engine_node',
    #     output='screen',
    #     parameters=[
    #         ship_config_path,              # [架构修复] yaml 垫底
    #         {'use_sim_time': use_sim_time},
    #         {'Hs': 0.0},                   # 强制覆盖 yaml 中的波高
    #         {'Tz': 0.1},
    #         {'direction_rad': 0.0},
    #         {'v_circ_surf': 0.0}
    #     ]
    # )
    #
    current_engine_node = Node(
        package='env_engines',
        executable='current_engine_node',
        name='current_engine_node',
        output='screen',
        condition=IfCondition(enable_current),
        parameters=[
            ship_config_path,
            {'use_sim_time': use_sim_time},
            {'v_tide_surf': ParameterValue(current_speed, value_type=float)},
            {'dir_tide': ParameterValue(current_direction, value_type=float)},
            {'current_direction_is_from': ParameterValue(current_direction_is_from, value_type=bool)},
            {'coeffs_csv_path': current_coeffs_csv_path},
            {'v_wind_surf': 0.0},
            {'v_circ_surf': 0.0}
        ]
    )

    force_aggregator_node = Node(
        package='env_engines',
        executable='force_aggregator_node',
        name='force_aggregator_node',
        output='screen',
        condition=IfCondition(enable_environment),
        parameters=[
            {'use_sim_time': use_sim_time},
            ship_config_path
        ]
    )

    ship_dynamics_node = Node(
        package='ship_dynamics',
        executable='ship_dynamics_node',
        name='ship_dynamics_node',
        namespace='',
        output='screen',
        parameters=[
            {'use_sim_time': use_sim_time},
            ship_config_path,
            {'initial_position.x': 0.0},  # [修复] 强制从原点启动
            {'initial_position.y': 0.0},
            {'initial_position.yaw': 0.0},
            {'auto_initial_yaw_from_route': True},
            {'auto_initial_yaw_min_segment_m': 20.0},
            {'auto_initial_yaw_max_speed_mps': 0.05},
            {'auto_initial_yaw_max_position_offset_m': 2.0}
        ]
    )

    # sensor_fusion_node = Node(
    #     package='sensor_fusion',
    #     executable='sensor_fusion_node',
    #     name='sensor_fusion_node',
    #     output='screen',
    #     parameters=[
    #         {'use_sim_time': use_sim_time},
    #         ship_config_path
    #     ]
    # )

    ship_guidance_node = Node(
        package='ship_guidance',
        executable='ship_guidance_node',
        name='ship_guidance_node',
        output='screen',
        parameters=[
            {'use_sim_time': use_sim_time},
            ship_config_path,
            {'use_adaptive_los': True},
            {'wait_for_route_plan': True}
        ]
    )

    coordinate_transform_node = Node(
        package='ship_guidance',
        executable='coordinate_transform_node',
        name='coordinate_transform_node',
        output='screen',
        parameters=[
            {'use_sim_time': use_sim_time},
            ship_config_path,
            {'enable_route_update_guard': True},
            {'min_route_update_interval_s': 10.0},
            {'min_future_update_distance_m': 500.0},
            {'max_dynamic_lateral_delta_m': 100.0},
            {'reject_reverse_segments': True}
        ]
    )

    active_route_manager_node = Node(
        package='ship_guidance',
        executable='active_route_manager_node',
        name='active_route_manager_node',
        output='screen',
        parameters=[
            {'use_sim_time': use_sim_time},
            ship_config_path
        ]
    )

    thrust_allocation_node = Node(
        package='thrust_allocation',
        executable='thrust_allocation_node',
        name='thrust_allocation_node',
        output='screen',
        parameters=[
            {'use_sim_time': use_sim_time},
            ship_config_path,
            {'env_feedforward_weight': ParameterValue(env_feedforward_weight, value_type=float)}
        ]
    )

    propulsion_policy_node = Node(
        package='mission_supervisor',
        executable='propulsion_policy_node',
        name='propulsion_policy_node',
        output='screen',
        parameters=[
            {'use_sim_time': use_sim_time},
            {'config_file': propulsion_policy_config_path},
            {'shadow_mode': True}
        ]
    )

    ship_control_node = Node(
        package='ship_control',
        executable='ship_control_node',
        name='ship_control_node',
        output='screen',
        parameters=[
            ship_config_path,
            {'use_sim_time': use_sim_time},
            {'ndo.enable': False}  # [修复] 静水测试时关闭NDO干扰
        ]
    )

    robot_state_publisher_node = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[{
            'use_sim_time': use_sim_time,
            'robot_description': robot_description_content
        }]
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'use_sim_time',
            default_value='false',
            description='Use simulation (Gazebo) clock if true'
        ),
        DeclareLaunchArgument(
            'enable_wind',
            default_value='false',
            description='Enable wind_engine_node and force_aggregator_node for wind-load tests'
        ),
        DeclareLaunchArgument(
            'enable_current',
            default_value='false',
            description='Enable current_engine_node and force_aggregator_node for current-load tests'
        ),
        DeclareLaunchArgument(
            'wind_u10',
            default_value='0.0',
            description='10m reference wind speed in m/s; only used when enable_wind=true'
        ),
        DeclareLaunchArgument(
            'wind_direction',
            default_value='0.0',
            description='Anemometer/meteorological wind direction in degrees: direction the wind comes from'
        ),
        DeclareLaunchArgument(
            'wind_direction_is_from',
            default_value='true',
            description='true: wind_direction is the anemometer/met direction the wind comes from; false: direction wind blows toward'
        ),
        DeclareLaunchArgument(
            'current_speed',
            default_value='0.0',
            description='Tide/current speed in m/s; only used when enable_current=true'
        ),
        DeclareLaunchArgument(
            'current_direction',
            default_value='0.0',
            description='Current direction in degrees; convention selected by current_direction_is_from'
        ),
        DeclareLaunchArgument(
            'current_direction_is_from',
            default_value='true',
            description='true: current_direction is where current comes from; false: where current goes to'
        ),
        DeclareLaunchArgument(
            'env_feedforward_weight',
            default_value='0.0',
            description='Environment-load feedforward weight in thrust allocation; keep 0.0 for first disturbance tests'
        ),

        wind_engine_node,
        # wave_engine_node,
        current_engine_node,
        force_aggregator_node,
        # === 静水测试：只启动核心GNC节点 ===
        ship_dynamics_node,
    #    sensor_fusion_node,
        active_route_manager_node,
        coordinate_transform_node,
        ship_guidance_node,
        propulsion_policy_node,
        thrust_allocation_node,
        ship_control_node,
        robot_state_publisher_node,
    ])
