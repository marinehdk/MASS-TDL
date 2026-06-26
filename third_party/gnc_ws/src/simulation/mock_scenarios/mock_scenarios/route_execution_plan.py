import math


def _as_point(value):
    return (float(value[0]), float(value[1]))


def _distance(a, b):
    return math.hypot(float(b[0]) - float(a[0]), float(b[1]) - float(a[1]))


def _cumulative_lengths(points):
    lengths = [0.0]
    for index in range(1, len(points)):
        lengths.append(lengths[-1] + _distance(points[index - 1], points[index]))
    return lengths


def _route_plan_points(scenario):
    route_plan = scenario.get('route_plan', {}) or {}
    waypoint_plan = route_plan.get('waypoint_plan', {}) or {}
    points = waypoint_plan.get('points') if isinstance(waypoint_plan, dict) else None
    if isinstance(points, list) and len(points) >= 2:
        ordered = sorted(
            points,
            key=lambda item: int(item.get('index', 0)) if isinstance(item, dict) else 0,
        )
        result = []
        ids = []
        roles = []
        for item in ordered:
            if not isinstance(item, dict):
                continue
            xy = item.get('local_xy_m')
            if isinstance(xy, list) and len(xy) == 2:
                result.append(_as_point(xy))
                ids.append(str(item.get('id', f'wp{len(result) - 1}')))
                roles.append(str(item.get('role', '')))
        if len(result) >= 2:
            return result, ids, roles

    waypoints = scenario.get('own_ship', {}).get('waypoints') or []
    result = [_as_point(item) for item in waypoints]
    ids = [f'wp{index}' for index, _point in enumerate(result)]
    roles = ['' for _point in result]
    return result, ids, roles


def route_execution_enabled(scenario):
    route_plan = scenario.get('route_plan', {}) or {}
    route_execution = route_plan.get('route_execution', {}) or {}
    return bool(route_execution.get('enabled', False))


def _leg_constraints(scenario, source_index):
    route_plan = scenario.get('route_plan', {}) or {}
    leg_plan = route_plan.get('leg_plan') or []
    if 0 <= source_index < len(leg_plan) and isinstance(leg_plan[source_index], dict):
        leg = leg_plan[source_index]
        xtl_port = float(leg.get('xtl_port_m', 100.0))
        xtl_starboard = float(leg.get('xtl_starboard_m', xtl_port))
        speed_limit = float(leg.get('speed_limit_mps', scenario.get('own_ship', {}).get('nominal_speed_mps', 3.0)))
        return {
            'source_leg_index': source_index,
            'source_leg_id': str(leg.get('id', f'leg{source_index}')),
            'xtl_m': max(xtl_port, xtl_starboard),
            'speed_limit_mps': speed_limit,
            'navigation_mode': str(leg.get('navigation_mode', '')),
        }
    return {
        'source_leg_index': source_index,
        'source_leg_id': f'leg{source_index}',
        'xtl_m': 100.0,
        'speed_limit_mps': float(scenario.get('own_ship', {}).get('nominal_speed_mps', 3.0)),
        'navigation_mode': '',
    }


def _mode_parameter(route_execution_cfg, key, navigation_mode, default_value):
    values = route_execution_cfg.get(f'{key}_by_navigation_mode') or {}
    if isinstance(values, dict) and navigation_mode in values:
        return float(values[navigation_mode])
    return float(default_value)


def _tangent(points, stations, index, tangent_scale):
    if len(points) < 2:
        return (0.0, 0.0)
    if index <= 0:
        span = max(stations[1] - stations[0], 1.0)
        return (
            tangent_scale * (points[1][0] - points[0][0]) / span,
            tangent_scale * (points[1][1] - points[0][1]) / span,
        )
    if index >= len(points) - 1:
        span = max(stations[-1] - stations[-2], 1.0)
        return (
            tangent_scale * (points[-1][0] - points[-2][0]) / span,
            tangent_scale * (points[-1][1] - points[-2][1]) / span,
        )
    span = max(stations[index + 1] - stations[index - 1], 1.0)
    return (
        tangent_scale * (points[index + 1][0] - points[index - 1][0]) / span,
        tangent_scale * (points[index + 1][1] - points[index - 1][1]) / span,
    )


def _hermite(p0, p1, m0, m1, length, t):
    h00 = 2.0 * t * t * t - 3.0 * t * t + 1.0
    h10 = t * t * t - 2.0 * t * t + t
    h01 = -2.0 * t * t * t + 3.0 * t * t
    h11 = t * t * t - t * t
    return (
        h00 * p0[0] + h10 * length * m0[0] + h01 * p1[0] + h11 * length * m1[0],
        h00 * p0[1] + h10 * length * m0[1] + h01 * p1[1] + h11 * length * m1[1],
    )


def _append_unique(points, point, min_gap=1.0e-6):
    if not points or _distance(points[-1], point) > min_gap:
        points.append(point)
        return True
    return False


def _unit_vector(a, b):
    length = _distance(a, b)
    if length < 1.0e-9:
        return (0.0, 0.0)
    return ((float(b[0]) - float(a[0])) / length, (float(b[1]) - float(a[1])) / length)


def _dot(a, b):
    return float(a[0]) * float(b[0]) + float(a[1]) * float(b[1])


def _add(a, b):
    return (float(a[0]) + float(b[0]), float(a[1]) + float(b[1]))


def _scale(v, value):
    return (float(v[0]) * float(value), float(v[1]) * float(value))


def _turn_angle_deg(prev_point, turn_point, next_point):
    inbound = _unit_vector(prev_point, turn_point)
    outbound = _unit_vector(turn_point, next_point)
    value = max(-1.0, min(1.0, _dot(inbound, outbound)))
    return math.degrees(math.acos(value))


def _append_sample(samples, sample_source_leg, point, source_leg):
    if _append_unique(samples, point, min_gap=0.1):
        sample_source_leg.append(max(0, int(source_leg)))


def _append_line_samples(samples, sample_source_leg, start, end, source_leg, sample_spacing):
    length = _distance(start, end)
    if length < 0.1:
        _append_sample(samples, sample_source_leg, end, source_leg)
        return

    step_count = max(1, int(math.ceil(length / sample_spacing)))
    for step in range(step_count + 1):
        t = step / step_count
        point = (
            float(start[0]) + (float(end[0]) - float(start[0])) * t,
            float(start[1]) + (float(end[1]) - float(start[1])) * t,
        )
        _append_sample(samples, sample_source_leg, point, source_leg)


def _append_quadratic_turn_samples(
    samples,
    sample_source_leg,
    entry,
    control,
    exit_point,
    inbound_leg,
    outbound_leg,
    sample_spacing,
):
    approx_length = _distance(entry, control) + _distance(control, exit_point)
    if approx_length < 0.1:
        _append_sample(samples, sample_source_leg, exit_point, outbound_leg)
        return

    step_count = max(4, int(math.ceil(approx_length / sample_spacing)))
    for step in range(1, step_count + 1):
        t = step / step_count
        one_minus_t = 1.0 - t
        point = (
            one_minus_t * one_minus_t * float(entry[0])
            + 2.0 * one_minus_t * t * float(control[0])
            + t * t * float(exit_point[0]),
            one_minus_t * one_minus_t * float(entry[1])
            + 2.0 * one_minus_t * t * float(control[1])
            + t * t * float(exit_point[1]),
        )
        source_leg = inbound_leg if t < 0.5 else outbound_leg
        _append_sample(samples, sample_source_leg, point, source_leg)


def _turn_plan_by_waypoint_id(scenario):
    route_plan = scenario.get('route_plan', {}) or {}
    turn_plan = route_plan.get('turn_plan') or []
    result = {}
    for item in turn_plan:
        if isinstance(item, dict) and item.get('at'):
            result[str(item.get('at'))] = item
    return result


def _corner_cut_distance(scenario, source_ids, source_points, corner_index):
    route_plan = scenario.get('route_plan', {}) or {}
    cfg = route_plan.get('route_execution', {}) or {}
    turn_by_id = _turn_plan_by_waypoint_id(scenario)
    turn_cfg = turn_by_id.get(source_ids[corner_index], {})

    default_cut = float(cfg.get('default_wheel_over_distance_m', cfg.get('corner_cut_distance_m', 180.0)))
    wheel_over = float(turn_cfg.get('wheel_over_distance_m', default_cut))
    min_cut = float(cfg.get('min_corner_cut_distance_m', 20.0))
    max_ratio = max(0.05, min(float(cfg.get('max_corner_cut_ratio', 0.45)), 0.49))

    prev_len = _distance(source_points[corner_index - 1], source_points[corner_index])
    next_len = _distance(source_points[corner_index], source_points[corner_index + 1])
    max_cut = max(0.0, min(prev_len, next_len) * max_ratio)
    if max_cut <= 0.0:
        return 0.0
    return max(0.0, min(max(wheel_over, min_cut), max_cut))


def _build_turn_aware_samples(source_points, source_ids, scenario):
    route_plan = scenario.get('route_plan', {}) or {}
    cfg = route_plan.get('route_execution', {}) or {}
    sample_spacing = max(float(cfg.get('sample_spacing_m', 80.0)), 15.0)
    min_turn_angle = max(0.0, float(cfg.get('min_turn_angle_deg', 1.0)))

    corner_entries = {}
    corner_exits = {}
    for index in range(1, len(source_points) - 1):
        angle = _turn_angle_deg(source_points[index - 1], source_points[index], source_points[index + 1])
        if angle < min_turn_angle:
            continue

        cut = _corner_cut_distance(scenario, source_ids, source_points, index)
        if cut <= 0.0:
            continue

        inbound = _unit_vector(source_points[index - 1], source_points[index])
        outbound = _unit_vector(source_points[index], source_points[index + 1])
        corner_entries[index] = _add(source_points[index], _scale(inbound, -cut))
        corner_exits[index] = _add(source_points[index], _scale(outbound, cut))

    samples = []
    sample_source_leg = []
    current = source_points[0]
    _append_sample(samples, sample_source_leg, current, 0)

    for leg_index in range(len(source_points) - 1):
        corner_index = leg_index + 1
        line_end = corner_entries.get(corner_index, source_points[corner_index])
        _append_line_samples(samples, sample_source_leg, current, line_end, leg_index, sample_spacing)

        if corner_index in corner_entries:
            exit_point = corner_exits[corner_index]
            _append_quadratic_turn_samples(
                samples,
                sample_source_leg,
                corner_entries[corner_index],
                source_points[corner_index],
                exit_point,
                max(0, corner_index - 1),
                corner_index,
                sample_spacing,
            )
            current = exit_point
        else:
            current = source_points[corner_index]

    if _append_unique(samples, source_points[-1], min_gap=0.1):
        sample_source_leg.append(max(0, len(source_points) - 2))
    return samples, sample_source_leg


def _build_smooth_samples(source_points, scenario):
    route_plan = scenario.get('route_plan', {}) or {}
    cfg = route_plan.get('route_execution', {}) or {}
    sample_spacing = max(float(cfg.get('sample_spacing_m', 80.0)), 15.0)
    tangent_scale = max(0.0, min(float(cfg.get('tangent_scale', 0.65)), 1.0))
    source_stations = _cumulative_lengths(source_points)
    tangents = [
        _tangent(source_points, source_stations, index, tangent_scale)
        for index in range(len(source_points))
    ]

    samples = []
    sample_source_leg = []
    for index in range(len(source_points) - 1):
        p0 = source_points[index]
        p1 = source_points[index + 1]
        length = max(source_stations[index + 1] - source_stations[index], 1.0)
        step_count = max(1, int(math.ceil(length / sample_spacing)))
        for step in range(step_count):
            t = step / step_count
            point = _hermite(p0, p1, tangents[index], tangents[index + 1], length, t)
            if not samples or _distance(samples[-1], point) > 1.0:
                samples.append(point)
                sample_source_leg.append(index)
    if _append_unique(samples, source_points[-1], min_gap=0.1):
        sample_source_leg.append(max(0, len(source_points) - 2))
    return samples, sample_source_leg


def _build_guidance_gates(scenario, waypoints, source_leg_by_point):
    route_plan = scenario.get('route_plan', {}) or {}
    cfg = route_plan.get('route_execution', {}) or {}
    default_switch_radius = float(cfg.get('switch_radius_m', 55.0))
    default_lookahead = float(cfg.get('lookahead_m', 130.0))
    max_switch_xte_ratio = float(cfg.get('max_switch_xte_ratio', 0.75))
    rejoin_ratio = float(cfg.get('rejoin_ratio', 0.45))
    gate_speed_ratio = float(cfg.get('gate_blocked_speed_ratio', 0.70))
    gate_speed_floor = float(cfg.get('min_gate_blocked_speed_mps', 2.4))
    missed_after_distance = float(cfg.get('missed_after_distance_m', 90.0))

    gates = []
    for index in range(1, len(waypoints)):
        source_leg = source_leg_by_point[min(index - 1, len(source_leg_by_point) - 1)]
        constraints = _leg_constraints(scenario, source_leg)
        xtl = constraints['xtl_m']
        speed_limit = constraints['speed_limit_mps']
        lookahead = _mode_parameter(
            cfg,
            'lookahead_m',
            constraints.get('navigation_mode', ''),
            default_lookahead,
        )
        if index == len(waypoints) - 1:
            gates.append({
                'index': index,
                'mode': 'final_hold',
                'switch_radius_m': float(cfg.get('final_switch_radius_m', 35.0)),
            })
            continue
        gates.append({
            'index': index,
            'mode': 'internal_reference',
            'switch_radius_m': default_switch_radius,
            'max_cross_track_m': max(25.0, min(xtl, xtl * max_switch_xte_ratio)),
            'missed_after_distance_m': missed_after_distance,
            'speed_limit_mps': speed_limit,
            'lookahead_m': lookahead,
            'rejoin_cross_track_m': max(15.0, xtl * rejoin_ratio),
            'gate_blocked_speed_mps': min(speed_limit, max(gate_speed_floor, speed_limit * gate_speed_ratio)),
            'source_leg_id': constraints['source_leg_id'],
            'source_navigation_mode': constraints['navigation_mode'],
        })
    return gates


def build_route_execution_plan(scenario):
    source_points, source_ids, source_roles = _route_plan_points(scenario)
    if len(source_points) < 2:
        raise ValueError('RouteExecutionPlan requires at least two source points')

    if route_execution_enabled(scenario):
        route_plan = scenario.get('route_plan', {}) or {}
        cfg = route_plan.get('route_execution', {}) or {}
        requested_mode = str(cfg.get('mode', 'smooth_reference_path'))
        if requested_mode in {'turn_aware_smooth_path', 'rounded_turn_path'}:
            guidance_points, source_leg_by_point = _build_turn_aware_samples(source_points, source_ids, scenario)
            mode = 'turn_aware_smooth_path'
        else:
            guidance_points, source_leg_by_point = _build_smooth_samples(source_points, scenario)
            mode = 'smooth_reference_path'
    else:
        guidance_points = source_points
        source_leg_by_point = [max(0, index) for index in range(len(guidance_points))]
        mode = 'legacy_waypoints'

    constraints = [
        _leg_constraints(scenario, source_leg_by_point[min(index, len(source_leg_by_point) - 1)])
        for index in range(max(0, len(guidance_points) - 1))
    ]
    stations = _cumulative_lengths(guidance_points)
    gates = _build_guidance_gates(scenario, guidance_points, source_leg_by_point)

    return {
        'enabled': route_execution_enabled(scenario),
        'mode': mode,
        'source_points': source_points,
        'source_ids': source_ids,
        'source_roles': source_roles,
        'guidance_waypoints': guidance_points,
        'guidance_stations_m': stations,
        'guidance_gates': gates,
        'segment_constraints': constraints,
    }


def guidance_waypoints_for_scenario(scenario):
    return build_route_execution_plan(scenario)['guidance_waypoints']
