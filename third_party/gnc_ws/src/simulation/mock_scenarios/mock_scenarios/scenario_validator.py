from pathlib import Path
import sys

import yaml


REQUIRED_TOP_LEVEL = [
    'scenario_id',
    'scenario_class',
    'validation_objective',
    'duration_s',
    'data_policy',
    'own_ship',
    'environment',
    'sensors',
    'expected',
]
REQUIRED_OWN_SHIP = ['initial_pose', 'initial_velocity', 'waypoints']
ALLOWED_SCENARIO_CLASSES = {
    'baseline',
    'standard_manoeuvre',
    'environmental_disturbance',
    'sensor_fault',
    'actuator_fault',
    'captain_operational',
}


def _is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _require_number(mapping, key, label, errors, positive=False):
    value = mapping.get(key)
    if not _is_number(value):
        errors.append(f'{label}.{key} must be numeric')
        return
    if positive and value <= 0:
        errors.append(f'{label}.{key} must be positive')


def _validate_route_plan(data, errors):
    route_plan = data.get('route_plan')
    if route_plan is None:
        return
    if not isinstance(route_plan, dict):
        errors.append('route_plan must be a mapping')
        return

    if route_plan.get('schema_version') != 'route_plan.v1':
        errors.append('route_plan.schema_version must be route_plan.v1')
    if route_plan.get('parameter_source') != 'mock_data':
        errors.append('route_plan.parameter_source must be mock_data')
    if route_plan.get('parameter_confidence') != 'C':
        errors.append('route_plan.parameter_confidence must be C')

    captain_intent = route_plan.get('captain_intent')
    if not isinstance(captain_intent, dict):
        errors.append('route_plan.captain_intent must be a mapping')
    else:
        _require_number(
            captain_intent,
            'nominal_cruise_speed_mps',
            'route_plan.captain_intent',
            errors,
            positive=True,
        )
        if 'speed_is_hard_limit' not in captain_intent:
            errors.append('route_plan.captain_intent.speed_is_hard_limit is required')

    waypoint_plan = route_plan.get('waypoint_plan', {})
    points = waypoint_plan.get('points') if isinstance(waypoint_plan, dict) else None
    if not isinstance(points, list) or len(points) < 2:
        errors.append('route_plan.waypoint_plan.points must contain at least two points')
        points = []

    own_ship_waypoints = data.get('own_ship', {}).get('waypoints', [])
    point_ids = set()
    point_indexes = set()
    for item in points:
        if not isinstance(item, dict):
            errors.append('route_plan.waypoint_plan.points entries must be mappings')
            continue
        point_id = item.get('id')
        index = item.get('index')
        xy = item.get('local_xy_m')
        label = f'route_plan.waypoint_plan.points[{point_id}]'

        if not point_id:
            errors.append('route_plan.waypoint_plan.points entry missing id')
        elif point_id in point_ids:
            errors.append(f'duplicate route_plan waypoint id: {point_id}')
        else:
            point_ids.add(point_id)

        if not isinstance(index, int):
            errors.append(f'{label}.index must be an integer')
        elif index in point_indexes:
            errors.append(f'duplicate route_plan waypoint index: {index}')
        else:
            point_indexes.add(index)

        if (
            not isinstance(xy, list)
            or len(xy) != 2
            or not all(_is_number(value) for value in xy)
        ):
            errors.append(f'{label}.local_xy_m must contain two numeric values')
        elif isinstance(index, int) and 0 <= index < len(own_ship_waypoints):
            legacy_xy = own_ship_waypoints[index]
            if len(legacy_xy) == 2:
                mismatch = max(abs(float(xy[i]) - float(legacy_xy[i])) for i in range(2))
                if mismatch > 1.0e-6:
                    errors.append(
                        f'{label}.local_xy_m must match own_ship.waypoints[{index}]'
                    )

        if item.get('coordinate_confidence') != 'C':
            errors.append(f'{label}.coordinate_confidence must be C')

    leg_plan = route_plan.get('leg_plan')
    if not isinstance(leg_plan, list) or not leg_plan:
        errors.append('route_plan.leg_plan must contain at least one leg')
        leg_plan = []

    leg_ids = set()
    for item in leg_plan:
        if not isinstance(item, dict):
            errors.append('route_plan.leg_plan entries must be mappings')
            continue
        leg_id = item.get('id')
        label = f'route_plan.leg_plan[{leg_id}]'
        if leg_id:
            leg_ids.add(leg_id)
        for endpoint in ('from', 'to'):
            if item.get(endpoint) not in point_ids:
                errors.append(f'{label}.{endpoint} must reference a waypoint id')
        _require_number(item, 'planned_cog_deg', label, errors)
        _require_number(item, 'distance_m', label, errors, positive=True)
        _require_number(item, 'speed_limit_mps', label, errors, positive=True)
        _require_number(item, 'xtl_port_m', label, errors, positive=True)
        _require_number(item, 'xtl_starboard_m', label, errors, positive=True)
        if not item.get('navigation_mode'):
            errors.append(f'{label}.navigation_mode is required')

    turn_plan = route_plan.get('turn_plan')
    if len(points) > 2 and not isinstance(turn_plan, list):
        errors.append('route_plan.turn_plan must be a list for multi-leg routes')
        turn_plan = []
    for item in turn_plan or []:
        if not isinstance(item, dict):
            errors.append('route_plan.turn_plan entries must be mappings')
            continue
        label = f"route_plan.turn_plan[{item.get('at')}]"
        if item.get('at') not in point_ids:
            errors.append(f'{label}.at must reference a waypoint id')
        for leg_ref in ('inbound_leg', 'outbound_leg'):
            if item.get(leg_ref) not in leg_ids:
                errors.append(f'{label}.{leg_ref} must reference a leg id')
        if item.get('turn_direction') not in {'port', 'starboard'}:
            errors.append(f'{label}.turn_direction must be port or starboard')
        _require_number(item, 'turn_radius_m', label, errors, positive=True)
        _require_number(item, 'max_rot_deg_s', label, errors, positive=True)
        _require_number(item, 'wheel_over_distance_m', label, errors, positive=True)
        _require_number(item, 'target_speed_mps', label, errors, positive=True)
        if item.get('parameter_confidence') != 'C':
            errors.append(f'{label}.parameter_confidence must be C')

    safety_corridor = route_plan.get('safety_corridor')
    if not isinstance(safety_corridor, dict):
        errors.append('route_plan.safety_corridor must be a mapping')
    else:
        for key in ('caution_ratio', 'hold_ratio', 'abort_ratio'):
            _require_number(safety_corridor, key, 'route_plan.safety_corridor', errors, positive=True)
        if not isinstance(safety_corridor.get('response_sequence'), list):
            errors.append('route_plan.safety_corridor.response_sequence must be a list')

    validation_policy = route_plan.get('validation_policy')
    if not isinstance(validation_policy, dict):
        errors.append('route_plan.validation_policy must be a mapping')
    elif validation_policy.get('require_route_shape_coverage') and (
        'min_route_side_coverage_ratio' not in data.get('expected', {})
    ):
        errors.append(
            'expected.min_route_side_coverage_ratio is required when route shape coverage is required'
        )


def validate_file(path):
    with open(path, 'r', encoding='utf-8') as stream:
        data = yaml.safe_load(stream) or {}

    errors = []
    for key in REQUIRED_TOP_LEVEL:
        if key not in data:
            errors.append(f'missing top-level key: {key}')

    scenario_class = data.get('scenario_class')
    if scenario_class and scenario_class not in ALLOWED_SCENARIO_CLASSES:
        errors.append(
            'scenario_class must be one of: '
            + ', '.join(sorted(ALLOWED_SCENARIO_CLASSES))
        )

    data_policy = data.get('data_policy', {})
    if not isinstance(data_policy, dict):
        errors.append('data_policy must be a mapping')
    else:
        if data_policy.get('parameter_source') != 'mock_data':
            errors.append('data_policy.parameter_source must be mock_data')
        if data_policy.get('parameter_confidence') != 'C':
            errors.append('data_policy.parameter_confidence must be C for mock scenarios')
        if not data_policy.get('real_data_transition'):
            errors.append('data_policy.real_data_transition is required')

    own_ship = data.get('own_ship', {})
    for key in REQUIRED_OWN_SHIP:
        if key not in own_ship:
            errors.append(f'missing own_ship key: {key}')

    waypoints = own_ship.get('waypoints', [])
    if not isinstance(waypoints, list) or len(waypoints) < 2:
        errors.append('own_ship.waypoints must contain at least two points')

    _validate_route_plan(data, errors)

    expected = data.get('expected', {})
    if not isinstance(expected, dict) or not expected:
        errors.append('expected must be a non-empty mapping')
    else:
        if 'pass_fail_basis' not in expected:
            errors.append('expected.pass_fail_basis is required')
        numeric_limits = [
            value for value in expected.values()
            if isinstance(value, (int, float)) and not isinstance(value, bool)
        ]
        if not numeric_limits:
            errors.append('expected must include at least one numeric acceptance limit')

    if errors:
        for error in errors:
            print(f'{path}: {error}')
        return False

    print(f'{path}: ok')
    return True


def main(args=None):
    args = args or sys.argv[1:]
    if not args:
        root = Path(__file__).resolve().parents[1]
        paths = sorted((root / 'config' / 'scenarios').glob('*.yaml'))
    else:
        paths = [Path(arg) for arg in args]

    ok = all(validate_file(path) for path in paths)
    raise SystemExit(0 if ok else 1)


if __name__ == '__main__':
    main()
