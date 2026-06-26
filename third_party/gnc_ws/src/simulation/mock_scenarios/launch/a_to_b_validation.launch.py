import math
import os
from pathlib import Path

import yaml
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from mock_scenarios.route_execution_plan import build_route_execution_plan


def _as_bool(value):
    return str(value).strip().lower() in {'1', 'true', 'yes', 'on'}


def _resolve_scenario_path(scenario_file, scenario_name):
    if scenario_file:
        path = Path(scenario_file)
        if path.exists():
            return path
    share = Path(get_package_share_directory('mock_scenarios'))
    return share / 'config' / 'scenarios' / scenario_name


def _load_yaml(path):
    with open(path, 'r', encoding='utf-8') as stream:
        return yaml.safe_load(stream) or {}


def _has_environment(scenario):
    env = scenario.get('environment', {}) or {}
    wind = env.get('wind', {}) or {}
    current = env.get('current', {}) or {}
    wave = env.get('wave', {}) or {}
    return (
        abs(float(wind.get('speed_mps', 0.0))) > 1e-6
        or abs(float(current.get('speed_mps', current.get('tide_speed_mps', 0.0)))) > 1e-6
        or abs(float(wave.get('hs_m', 0.0))) > 1e-6
    )


def _scenario_overrides(scenario, section, allowed):
    values = scenario.get('tuning', {}).get(section, {}) or {}
    overrides = []
    for key in allowed:
        value = values
        found = False
        if key in values:
            value = values[key]
            found = True
        else:
            for part in key.split('.'):
                if not isinstance(value, dict) or part not in value:
                    value = None
                    break
                value = value[part]
            found = value is not None
        if found:
            if isinstance(value, bool):
                overrides.append({key: value})
            else:
                overrides.append({key: float(value)})
    return overrides


def _route_gate_overrides(scenario, waypoint_count, route_execution=None):
    modes = ['overrun'] * waypoint_count
    switch_radius_m = [0.0] * waypoint_count
    wheel_over_distance_m = [0.0] * waypoint_count
    switch_max_xte_m = [0.0] * waypoint_count
    missed_after_distance_m = [0.0] * waypoint_count
    switch_max_heading_error_deg = [0.0] * waypoint_count
    switch_max_speed_mps = [0.0] * waypoint_count
    speed_limit_mps = [0.0] * waypoint_count
    lookahead_m = [0.0] * waypoint_count
    rejoin_cross_track_m = [0.0] * waypoint_count
    gate_blocked_speed_mps = [0.0] * waypoint_count
    navigation_modes = [''] * waypoint_count

    if waypoint_count > 0:
        modes[0] = 'start'
        modes[-1] = 'final_hold'

    if route_execution and route_execution.get('enabled'):
        gate_items = route_execution.get('guidance_gates', []) or []
    else:
        route_plan = scenario.get('route_plan', {}) or {}
        gate_items = route_plan.get('waypoint_gates', []) or []

    for gate in gate_items:
        index = int(gate.get('index', -1))
        if index < 0 or index >= waypoint_count:
            continue

        if 'mode' in gate:
            modes[index] = str(gate['mode'])
        if 'switch_radius_m' in gate:
            switch_radius_m[index] = float(gate['switch_radius_m'])
        if 'wheel_over_distance_m' in gate:
            wheel_over_distance_m[index] = float(gate['wheel_over_distance_m'])
        if 'max_cross_track_m' in gate:
            switch_max_xte_m[index] = float(gate['max_cross_track_m'])
        if 'missed_after_distance_m' in gate:
            missed_after_distance_m[index] = float(gate['missed_after_distance_m'])
        if 'max_heading_error_deg' in gate:
            switch_max_heading_error_deg[index] = float(gate['max_heading_error_deg'])
        if 'max_switch_speed_mps' in gate:
            switch_max_speed_mps[index] = float(gate['max_switch_speed_mps'])
        if 'speed_limit_mps' in gate:
            speed_limit_mps[index] = float(gate['speed_limit_mps'])
        if 'max_speed_mps' in gate:
            speed_limit_mps[index] = float(gate['max_speed_mps'])
        if 'lookahead_m' in gate:
            lookahead_m[index] = float(gate['lookahead_m'])
        if 'rejoin_cross_track_m' in gate:
            rejoin_cross_track_m[index] = float(gate['rejoin_cross_track_m'])
        if 'gate_blocked_speed_mps' in gate:
            gate_blocked_speed_mps[index] = float(gate['gate_blocked_speed_mps'])
        if 'source_navigation_mode' in gate:
            navigation_modes[index] = str(gate['source_navigation_mode'])
        elif 'navigation_mode' in gate:
            navigation_modes[index] = str(gate['navigation_mode'])

    return [
        {'wp_switch_modes': modes},
        {'wp_switch_radius_m': switch_radius_m},
        {'wp_wheel_over_distance_m': wheel_over_distance_m},
        {'wp_switch_max_xte_m': switch_max_xte_m},
        {'wp_missed_after_distance_m': missed_after_distance_m},
        {'wp_switch_max_heading_error_deg': switch_max_heading_error_deg},
        {'wp_switch_max_speed_mps': switch_max_speed_mps},
        {'wp_speed_limit_mps': speed_limit_mps},
        {'wp_lookahead_m': lookahead_m},
        {'wp_rejoin_cross_track_m': rejoin_cross_track_m},
        {'wp_gate_blocked_speed_mps': gate_blocked_speed_mps},
        {'wp_navigation_modes': navigation_modes},
    ]


def _make_nodes(context):
    scenario_file = LaunchConfiguration('scenario_file').perform(context)
    scenario_name = LaunchConfiguration('scenario_name').perform(context)
    environment_mode = LaunchConfiguration('environment_mode').perform(context)
    log_dir = LaunchConfiguration('log_dir').perform(context)
    use_sim_time = _as_bool(LaunchConfiguration('use_sim_time').perform(context))
    override_n_v = LaunchConfiguration('override_n_v').perform(context).strip()

    scenario_path = _resolve_scenario_path(scenario_file, scenario_name)
    scenario = _load_yaml(scenario_path)
    own_ship = scenario.get('own_ship', {}) or {}
    initial_pose = own_ship.get('initial_pose', {}) or {}
    initial_velocity = own_ship.get('initial_velocity', {}) or {}
    route_execution = build_route_execution_plan(scenario)
    waypoints = (
        route_execution.get('guidance_waypoints')
        or own_ship.get('waypoints')
        or [[0.0, 0.0], [100.0, 0.0]]
    )
    wp_x = [float(item[0]) for item in waypoints]
    wp_y = [float(item[1]) for item in waypoints]
    route_xte_limits = [
        float(item.get('xtl_m', 0.0))
        for item in (route_execution.get('segment_constraints') or [])
    ]
    route_gate_overrides = _route_gate_overrides(scenario, len(waypoints), route_execution)
    nominal_speed = float(own_ship.get('nominal_speed_mps', 3.0))
    yaw_rad = math.radians(float(initial_pose.get('yaw_deg', 0.0)))
    scenario_id = scenario.get('scenario_id', scenario_path.stem)
    guidance_overrides = _scenario_overrides(
        scenario,
        'guidance',
        {
            'intermediate_capture_radius',
            'intermediate_overrun_radius',
            'max_transit_speed',
            'minimum_steerage_speed',
            'use_adaptive_los',
            'kappa_ilos',
            'gamma_alos',
            'delta_min_coeff',
            'gamma_lookahead',
            'slow_down_dist',
            'final_capture_radius',
            'final_capture_speed',
            'final_handoff_speed',
            'final_capture_max_cross_track_m',
            'final_reacquire_cross_track_m',
            'final_reacquire_min_speed_mps',
            'terminal_decel_use_position_error',
            'final_approach_lookahead_m',
            'final_approach_use_adaptive_los',
            'turn_speed_15deg',
            'turn_speed_45deg',
            'turn_speed_90deg',
            'turn_speed_180deg',
            'turn_no_slowdown_angle_deg',
            'turn_slow_down_dist_15deg',
            'turn_slow_down_dist_45deg',
            'turn_slow_down_dist_90deg',
            'turn_slow_down_dist_180deg',
            'heading_cmd_rate_limit_deg_s',
            'homing_threshold_m',
            'homing_max_approach_angle_deg',
            'homing_lookahead_m',
            'turn_recovery_gate_enabled',
            'turn_recovery_require_cruise_mode',
            'turn_recovery_max_xte_m',
            'turn_recovery_max_heading_error_deg',
            'turn_recovery_max_yaw_rate_deg_s',
            'turn_recovery_max_cross_track_rate_mps',
            'turn_recovery_speed_margin_mps',
            'turn_recovery_speed_ramp_mps2',
            'reset_sideslip_on_waypoint_switch',
        },
    )
    control_overrides = _scenario_overrides(
        scenario,
        'control',
        {
            'min_yaw_moment_cruise',
            'min_yaw_moment_error_deg',
            'max_torque_z',
            'max_reverse_surge',
            'speed_drag_feedforward.linear_N_per_mps',
            'speed_drag_feedforward.quadratic_N_per_mps2',
            'gains.yaw.kp',
            'gains.yaw.ki',
            'gains.yaw.kd',
            'gains.yaw.k_robust',
            'gains.yaw.phi',
            'coordinate_conventions.yaw_torque_sign',
            'coordinate_conventions.yaw_torque_sign_cruise',
            'coordinate_conventions.yaw_torque_sign_dp',
        },
    )
    allocation_overrides = _scenario_overrides(
        scenario,
        'thrust_allocation',
        {
            'main_equalization_yaw_deadband_kNm',
            'main_equalization_lateral_deadband_kN',
            'side_thruster_derate_start_speed_mps',
            'side_thruster_derate_decay_per_mps',
            'side_thruster_lockout_speed_mps',
            'side_thruster_emergency_unlock_max_speed_mps',
            'side_thruster_lateral_deadband_kN',
            'side_thruster_yaw_emergency_kNm',
        },
    )

    if not log_dir.endswith('/'):
        log_dir = log_dir + '/'

    ship_config_path = os.path.join(
        get_package_share_directory('ship_bringup'),
        'config',
        'ship_config.yaml',
    )
    env_data_dir = os.path.join(get_package_share_directory('env_engines'), 'data')
    ship_urdf_path = os.path.join(
        get_package_share_directory('ship_description'),
        'urdf',
        'ship.urdf',
    )
    safety_config_path = os.path.join(
        get_package_share_directory('safety_supervisor'),
        'config',
        'safety_limits.yaml',
    )
    mission_config_path = os.path.join(
        get_package_share_directory('mission_supervisor'),
        'config',
        'mission_gates.yaml',
    )
    captain_decision_config_path = os.path.join(
        get_package_share_directory('mission_supervisor'),
        'config',
        'captain_decision_policy.yaml',
    )
    propulsion_policy_config_path = os.path.join(
        get_package_share_directory('mission_supervisor'),
        'config',
        'propulsion_policy.yaml',
    )
    propulsion_compliance_config_path = os.path.join(
        get_package_share_directory('mission_supervisor'),
        'config',
        'propulsion_policy_compliance.yaml',
    )
    with open(ship_urdf_path, 'r', encoding='utf-8') as stream:
        robot_description_content = stream.read()

    enable_environment = (
        _has_environment(scenario)
        if environment_mode == 'auto'
        else _as_bool(environment_mode)
    )
    dynamics_overrides = [
        {'use_sim_time': use_sim_time},
        {'initial_position.x': float(initial_pose.get('x', wp_x[0]))},
        {'initial_position.y': float(initial_pose.get('y', wp_y[0]))},
        {'initial_position.yaw': yaw_rad},
        {'initial_velocity.u': float(initial_velocity.get('u', 0.0))},
        {'initial_velocity.v': float(initial_velocity.get('v', 0.0))},
        {'initial_velocity.r': math.radians(float(initial_velocity.get('r_deg_s', 0.0)))},
        {'log_dir': log_dir},
    ]
    if override_n_v:
        dynamics_overrides.append({'vessel.hydrodynamic.N_v': float(override_n_v)})

    nodes = [
        Node(
            package='ship_dynamics',
            executable='ship_dynamics_node',
            name='ship_dynamics_node',
            output='screen',
            parameters=[
                ship_config_path,
                *dynamics_overrides,
            ],
        ),
        Node(
            package='ship_guidance',
            executable='ship_guidance_node',
            name='ship_guidance_node',
            output='screen',
            parameters=[
                ship_config_path,
                {'use_sim_time': use_sim_time},
                {'use_adaptive_los': True},
                {'max_transit_speed': nominal_speed},
                {'wp_x': wp_x},
                {'wp_y': wp_y},
                *guidance_overrides,
                *route_gate_overrides,
            ],
        ),
        Node(
            package='thrust_allocation',
            executable='thrust_allocation_node',
            name='thrust_allocation_node',
            output='screen',
            parameters=[
                ship_config_path,
                {'use_sim_time': use_sim_time},
                *allocation_overrides,
            ],
        ),
        Node(
            package='ship_control',
            executable='ship_control_node',
            name='ship_control_node',
            output='screen',
            parameters=[
                ship_config_path,
                {'use_sim_time': use_sim_time},
                {'ndo.enable': enable_environment},
                {'cruise_speed': nominal_speed},
                *control_overrides,
            ],
        ),
        Node(
            package='safety_supervisor',
            executable='safety_supervisor_node',
            name='safety_supervisor_node',
            output='screen',
            parameters=[
                {'config_file': safety_config_path},
                {'use_sim_time': use_sim_time},
                {'shadow_mode': True},
                {'phase': 'cruise'},
            ],
        ),
        Node(
            package='mission_supervisor',
            executable='mission_supervisor_node',
            name='mission_supervisor_node',
            output='screen',
            parameters=[
                {'config_file': mission_config_path},
                {'scenario_file': str(scenario_path)},
                {'use_sim_time': use_sim_time},
                {'shadow_mode': True},
                {'auto_advance': True},
                {'route_wp_x': wp_x},
                {'route_wp_y': wp_y},
                {'route_xte_limits_m': route_xte_limits},
            ],
        ),
        Node(
            package='mission_supervisor',
            executable='captain_decision_node',
            name='captain_decision_node',
            output='screen',
            parameters=[
                {'config_file': captain_decision_config_path},
                {'scenario_file': str(scenario_path)},
                {'use_sim_time': use_sim_time},
                {'shadow_mode': True},
            ],
        ),
        Node(
            package='mission_supervisor',
            executable='propulsion_policy_node',
            name='propulsion_policy_node',
            output='screen',
            parameters=[
                {'config_file': propulsion_policy_config_path},
                {'scenario_file': str(scenario_path)},
                {'use_sim_time': use_sim_time},
                {'shadow_mode': True},
            ],
        ),
        Node(
            package='mission_supervisor',
            executable='propulsion_policy_compliance_node',
            name='propulsion_policy_compliance_node',
            output='screen',
            parameters=[
                {'config_file': propulsion_compliance_config_path},
                {'scenario_file': str(scenario_path)},
                {'use_sim_time': use_sim_time},
                {'shadow_mode': True},
            ],
        ),
        Node(
            package='mock_scenarios',
            executable='scenario_runtime_publisher',
            name='scenario_runtime_publisher',
            output='screen',
            parameters=[
                {'scenario_file': str(scenario_path)},
                {'publish_environment': enable_environment},
                {'publish_waypoints': False},
                {'inject_faults': True},
            ],
        ),
        Node(
            package='mock_scenarios',
            executable='validation_observer',
            name='validation_observer',
            output='screen',
            parameters=[
                {'log_dir': log_dir},
                {'scenario_id': scenario_id},
                {'use_sim_time': use_sim_time},
            ],
        ),
        Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            name='robot_state_publisher',
            output='screen',
            parameters=[
                {
                    'use_sim_time': use_sim_time,
                    'robot_description': robot_description_content,
                }
            ],
        ),
    ]

    if enable_environment:
        env = scenario.get('environment', {}) or {}
        wind = env.get('wind', {}) or {}
        wave = env.get('wave', {}) or {}
        current = env.get('current', {}) or {}
        restricted = env.get('restricted_water', {}) or {}
        nodes.extend([
            Node(
                package='env_engines',
                executable='wind_engine_node',
                name='wind_engine_node',
                output='screen',
                parameters=[
                    ship_config_path,
                    {'use_sim_time': use_sim_time},
                    {'u10': float(wind.get('speed_mps', 0.0))},
                    {'wind_direction': float(wind.get('direction_deg', 0.0))},
                    {'anemometer_height': float(wind.get('anemometer_height_m', 10.0))},
                ],
            ),
            Node(
                package='env_engines',
                executable='wave_engine_node',
                name='wave_engine_node',
                output='screen',
                parameters=[
                    ship_config_path,
                    {'use_sim_time': use_sim_time},
                    {'Hs': float(wave.get('hs_m', 0.0))},
                    {'Tz': float(wave.get('tz_s', wave.get('Tz_s', 6.0)))},
                    {'direction_rad': math.radians(float(wave.get('direction_deg', 0.0)))},
                    {'qtf_csv_path': os.path.join(env_data_dir, 'wave_drift_qtfs.csv')},
                ],
            ),
            Node(
                package='env_engines',
                executable='current_engine_node',
                name='current_engine_node',
                output='screen',
                parameters=[
                    ship_config_path,
                    {'use_sim_time': use_sim_time},
                    {'v_tide_surf': float(current.get('tide_speed_mps', current.get('speed_mps', 0.0)))},
                    {'d': float(restricted.get('shallow_water_depth_m', 20.0))},
                    {'coeffs_csv_path': os.path.join(env_data_dir, 'current_coeffs.csv')},
                ],
            ),
            Node(
                package='env_engines',
                executable='force_aggregator_node',
                name='force_aggregator_node',
                output='screen',
                parameters=[ship_config_path, {'use_sim_time': use_sim_time}],
            ),
        ])

    print(
        f"[a_to_b_validation] scenario={scenario_id} path={scenario_path} "
        f"environment={enable_environment} route_execution={route_execution.get('mode')} "
        f"wp_count={len(waypoints)} log_dir={log_dir}"
    )
    return nodes


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            'scenario_name',
            default_value='001_straight_calm.yaml',
            description='Scenario YAML installed under mock_scenarios/config/scenarios.',
        ),
        DeclareLaunchArgument(
            'scenario_file',
            default_value='',
            description='Absolute scenario YAML path. Overrides scenario_name.',
        ),
        DeclareLaunchArgument(
            'environment_mode',
            default_value='auto',
            description='auto, true, or false. auto enables environment nodes when scenario has wind/current/wave.',
        ),
        DeclareLaunchArgument(
            'log_dir',
            default_value='/tmp/mass_adas_ab_validation/',
            description='Directory for ship_dynamics CSV logs.',
        ),
        DeclareLaunchArgument(
            'use_sim_time',
            default_value='false',
            description='Use simulated time.',
        ),
        DeclareLaunchArgument(
            'override_n_v',
            default_value='',
            description='Optional override for vessel.hydrodynamic.N_v in ship_dynamics only.',
        ),
        OpaqueFunction(function=_make_nodes),
    ])
