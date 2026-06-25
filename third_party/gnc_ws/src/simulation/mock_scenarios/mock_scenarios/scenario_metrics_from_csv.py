from pathlib import Path
import argparse
import csv
import math
import sys

import yaml

from mock_scenarios.route_execution_plan import build_route_execution_plan


def _load_yaml(path):
    with open(path, 'r', encoding='utf-8') as stream:
        return yaml.safe_load(stream) or {}


def _read_csv(path):
    with open(path, 'r', encoding='utf-8') as stream:
        return list(csv.DictReader(stream))


def _float(row, key, default=0.0):
    try:
        return float(row.get(key, default))
    except (TypeError, ValueError):
        return default


def _wrap_deg(angle):
    while angle > 180.0:
        angle -= 360.0
    while angle < -180.0:
        angle += 360.0
    return angle


def _segment_distance_and_heading(px, py, ax, ay, bx, by):
    dx = bx - ax
    dy = by - ay
    length2 = dx * dx + dy * dy
    if length2 < 1e-9:
        return math.hypot(px - ax, py - ay), 0.0
    t = ((px - ax) * dx + (py - ay) * dy) / length2
    t = max(0.0, min(1.0, t))
    cx = ax + t * dx
    cy = ay + t * dy
    heading = math.degrees(math.atan2(dy, dx))
    return math.hypot(px - cx, py - cy), heading


def _segment_heading(ax, ay, bx, by):
    return math.degrees(math.atan2(by - ay, bx - ax))


def _corner_heading_candidates(px, py, waypoints, radius_m):
    if radius_m <= 0.0:
        return []
    headings = []
    for index in range(1, len(waypoints) - 1):
        vx, vy = map(float, waypoints[index])
        if math.hypot(px - vx, py - vy) > radius_m:
            continue
        ax, ay = map(float, waypoints[index - 1])
        bx, by = map(float, waypoints[index + 1])
        headings.append(_segment_heading(ax, ay, vx, vy))
        headings.append(_segment_heading(vx, vy, bx, by))
    return headings


def _heading_sector_error(psi_deg, start_heading_deg, end_heading_deg):
    turn_span = _wrap_deg(end_heading_deg - start_heading_deg)
    psi_from_start = _wrap_deg(psi_deg - start_heading_deg)
    if turn_span >= 0.0:
        if 0.0 <= psi_from_start <= turn_span:
            return 0.0
    elif turn_span <= psi_from_start <= 0.0:
        return 0.0
    return min(
        abs(_wrap_deg(psi_deg - start_heading_deg)),
        abs(_wrap_deg(psi_deg - end_heading_deg)),
    )


def _corner_transition_heading_errors(px, py, psi_deg, waypoints, radius_m):
    if radius_m <= 0.0:
        return []
    errors = []
    for index in range(1, len(waypoints) - 1):
        vx, vy = map(float, waypoints[index])
        if math.hypot(px - vx, py - vy) > radius_m:
            continue
        ax, ay = map(float, waypoints[index - 1])
        bx, by = map(float, waypoints[index + 1])
        inbound = _segment_heading(ax, ay, vx, vy)
        outbound = _segment_heading(vx, vy, bx, by)
        errors.append(_heading_sector_error(psi_deg, inbound, outbound))
    return errors


def _route_error(row, waypoints):
    x = _float(row, 'x')
    y = _float(row, 'y')
    best_distance = float('inf')
    best_heading = 0.0
    for index in range(1, len(waypoints)):
        ax, ay = map(float, waypoints[index - 1])
        bx, by = map(float, waypoints[index])
        distance, heading = _segment_distance_and_heading(x, y, ax, ay, bx, by)
        if distance < best_distance:
            best_distance = distance
            best_heading = heading
    return best_distance, best_heading


def _route_error_with_segment(row, waypoints):
    x = _float(row, 'x')
    y = _float(row, 'y')
    best_distance = float('inf')
    best_heading = 0.0
    best_segment = 0
    for index in range(1, len(waypoints)):
        ax, ay = map(float, waypoints[index - 1])
        bx, by = map(float, waypoints[index])
        distance, heading = _segment_distance_and_heading(x, y, ax, ay, bx, by)
        if distance < best_distance:
            best_distance = distance
            best_heading = heading
            best_segment = index - 1
    return best_distance, best_heading, best_segment


def _waypoint_gate_xte_limits(scenario):
    waypoint_count = len(scenario.get('own_ship', {}).get('waypoints') or [])
    if waypoint_count <= 1:
        return []
    limits = [float('inf')] * waypoint_count
    route_plan = scenario.get('route_plan', {}) or {}
    for gate in route_plan.get('waypoint_gates', []) or []:
        try:
            index = int(gate.get('index'))
        except (TypeError, ValueError):
            continue
        if 0 <= index < waypoint_count and 'max_cross_track_m' in gate:
            limits[index] = float(gate['max_cross_track_m'])
    return limits


def _analysis_plan(scenario):
    return build_route_execution_plan(scenario)


def _analysis_waypoints(scenario):
    return _analysis_plan(scenario)['guidance_waypoints']


def _leg_plan_xte_limits(scenario, segment_count=None):
    waypoints = _analysis_waypoints(scenario)
    segment_count = max(0, len(waypoints) - 1) if segment_count is None else segment_count
    if segment_count <= 0:
        return []
    execution = _analysis_plan(scenario)
    if execution.get('enabled'):
        return [
            float(item.get('xtl_m', float('inf')))
            for item in execution.get('segment_constraints', [])[:segment_count]
        ]
    route_plan = scenario.get('route_plan', {}) or {}
    leg_plan = route_plan.get('leg_plan') or []
    limits = [float('inf')] * segment_count
    for index, leg in enumerate(leg_plan[:segment_count]):
        try:
            port = float(leg.get('xtl_port_m', float('inf')))
            starboard = float(leg.get('xtl_starboard_m', float('inf')))
        except (TypeError, ValueError):
            continue
        limits[index] = max(port, starboard)
    return limits


def _route_plan_xte_metrics(rows, waypoints, scenario):
    route_plan = scenario.get('route_plan', {}) or {}
    validation_policy = route_plan.get('validation_policy', {}) or {}
    safety_corridor = route_plan.get('safety_corridor', {}) or {}
    limit_source = validation_policy.get('xte_limit_source', 'waypoint_gates')
    try:
        caution_ratio = float(safety_corridor.get('caution_ratio', 1.0))
    except (TypeError, ValueError):
        caution_ratio = 1.0
    caution_ratio = max(0.0, caution_ratio)
    limits = (
        _leg_plan_xte_limits(scenario, max(0, len(waypoints) - 1))
        if limit_source == 'leg_plan'
        else _waypoint_gate_xte_limits(scenario)
    )
    if not limits:
        return {}

    max_excess = 0.0
    max_ratio = 0.0
    max_caution_excess = 0.0
    max_caution_ratio = 0.0
    worst_limit = 0.0
    worst_segment = ''
    worst_caution_limit = 0.0
    worst_caution_segment = ''
    for row in rows:
        cte, _heading, segment = _route_error_with_segment(row, waypoints)
        if limit_source == 'leg_plan':
            limit_index = min(segment, len(limits) - 1)
        else:
            limit_index = min(segment + 1, len(limits) - 1)
        limit = limits[limit_index]
        if not math.isfinite(limit) or limit <= 0.0:
            continue
        excess = max(0.0, cte - limit)
        ratio = cte / limit
        caution_limit = limit * caution_ratio
        caution_excess = max(0.0, cte - caution_limit) if caution_limit > 0.0 else 0.0
        current_caution_ratio = cte / caution_limit if caution_limit > 0.0 else 0.0
        if excess > max_excess or (excess == max_excess and ratio > max_ratio):
            max_excess = excess
            max_ratio = ratio
            worst_limit = limit
            worst_segment = f'exec{segment}-exec{segment + 1}' if route_plan.get('route_execution', {}).get('enabled') else f'wp{segment}-wp{segment + 1}'
        if current_caution_ratio > max_caution_ratio:
            max_caution_ratio = current_caution_ratio
            worst_caution_limit = caution_limit
            worst_caution_segment = f'exec{segment}-exec{segment + 1}' if route_plan.get('route_execution', {}).get('enabled') else f'wp{segment}-wp{segment + 1}'
        if caution_excess > max_caution_excess:
            max_caution_excess = caution_excess

    return {
        'max_route_plan_caution_excess_m': max_caution_excess,
        'max_route_plan_caution_ratio': max_caution_ratio,
        'max_route_plan_xte_excess_m': max_excess,
        'max_route_plan_xte_ratio': max_ratio,
        'route_plan_caution_limit_at_worst_m': worst_caution_limit,
        'route_plan_caution_ratio': caution_ratio,
        'route_plan_caution_worst_segment': worst_caution_segment,
        'route_plan_xte_limit_at_worst_m': worst_limit,
        'route_plan_xte_worst_segment': worst_segment,
        'route_plan_xte_limit_source': limit_source,
    }


def _route_chord_offsets(points, waypoints):
    sx, sy = map(float, waypoints[0])
    ex, ey = map(float, waypoints[-1])
    dx = ex - sx
    dy = ey - sy
    length = math.hypot(dx, dy)
    if length < 1e-9:
        return [0.0 for _point in points]
    return [
        (dx * (float(y) - sy) - dy * (float(x) - sx)) / length
        for x, y in points
    ]


def _side_coverage_metrics(rows, waypoints):
    route_offsets = _route_chord_offsets(waypoints, waypoints)
    track_points = [(_float(row, 'x'), _float(row, 'y')) for row in rows]
    track_offsets = _route_chord_offsets(track_points, waypoints)

    route_positive = max(route_offsets or [0.0])
    route_negative = abs(min(route_offsets or [0.0]))
    track_positive = max(track_offsets or [0.0])
    track_negative = abs(min(track_offsets or [0.0]))

    positive_gap = max(0.0, route_positive - track_positive)
    negative_gap = max(0.0, route_negative - track_negative)
    required_sides = [value for value in (route_positive, route_negative) if value > 1e-6]
    side_ratios = []
    if route_positive > 1e-6:
        side_ratios.append(track_positive / route_positive)
    if route_negative > 1e-6:
        side_ratios.append(track_negative / route_negative)

    return {
        'planned_positive_route_side_excursion_m': route_positive,
        'planned_negative_route_side_excursion_m': route_negative,
        'actual_positive_route_side_excursion_m': track_positive,
        'actual_negative_route_side_excursion_m': track_negative,
        'max_route_intent_lateral_gap_m': max(positive_gap, negative_gap),
        'min_route_side_coverage_ratio': min(side_ratios) if side_ratios else 1.0,
        'should_cover_route_lateral_sides': (
            not required_sides
            or all(ratio >= 0.5 for ratio in side_ratios)
        ),
    }


def _waypoint_coverage_metrics(rows, waypoints):
    points = [(_float(row, 'x'), _float(row, 'y')) for row in rows]
    min_distances = []
    for wx, wy in waypoints:
        min_distances.append(
            min(
                math.hypot(x - float(wx), y - float(wy))
                for x, y in points
            )
        )
    intermediate = min_distances[1:-1] if len(min_distances) > 2 else min_distances
    return {
        'waypoint_min_distances_m': [round(value, 3) for value in min_distances],
        'max_intermediate_waypoint_min_distance_m': max(intermediate or [0.0]),
    }


def _route_plan_point_map(scenario):
    route_plan = scenario.get('route_plan', {}) or {}
    waypoint_plan = route_plan.get('waypoint_plan', {}) or {}
    result = {}
    for item in waypoint_plan.get('points', []) or []:
        if not isinstance(item, dict):
            continue
        point_id = item.get('id')
        xy = item.get('local_xy_m')
        if point_id and isinstance(xy, list) and len(xy) == 2:
            result[str(point_id)] = (float(xy[0]), float(xy[1]))
    return result


def _post_turn_recovery_metrics(rows, scenario):
    route_plan = scenario.get('route_plan', {}) or {}
    leg_plan = route_plan.get('leg_plan') or []
    point_map = _route_plan_point_map(scenario)
    policy = route_plan.get('speed_recovery_policy', {}) or {}
    recovery_modes = set(
        policy.get(
            'applies_when_next_navigation_mode',
            ['cruise', 'open_water_cruise', 'post_turn_cruise'],
        )
        or []
    )
    try:
        speed_margin = float(policy.get('speed_margin_mps', 0.5))
    except (TypeError, ValueError):
        speed_margin = 0.5
    try:
        ignore_entry_distance_m = float(policy.get('validation_ignore_entry_distance_m', 120.0))
    except (TypeError, ValueError):
        ignore_entry_distance_m = 120.0

    recovery_legs = []
    for index, leg in enumerate(leg_plan):
        if not isinstance(leg, dict):
            continue
        mode = str(leg.get('navigation_mode', ''))
        if mode not in recovery_modes:
            continue
        previous_speed = None
        if index > 0 and isinstance(leg_plan[index - 1], dict):
            try:
                previous_speed = float(leg_plan[index - 1].get('speed_limit_mps'))
            except (TypeError, ValueError):
                previous_speed = None
        try:
            speed_limit = float(leg.get('speed_limit_mps'))
        except (TypeError, ValueError):
            continue
        if previous_speed is None or speed_limit <= previous_speed + speed_margin:
            continue
        start = point_map.get(str(leg.get('from')))
        end = point_map.get(str(leg.get('to')))
        if not start or not end:
            continue
        recovery_legs.append((start, end))

    if not recovery_legs:
        return {}

    peak_speed = 0.0
    observed = False
    for row in rows:
        x = _float(row, 'x')
        y = _float(row, 'y')
        for start, end in recovery_legs:
            ax, ay = start
            bx, by = end
            dx = bx - ax
            dy = by - ay
            length = math.hypot(dx, dy)
            if length < 1e-6:
                continue
            s = ((x - ax) * dx + (y - ay) * dy) / length
            if s < ignore_entry_distance_m or s > length:
                continue
            cte, _heading = _segment_distance_and_heading(x, y, ax, ay, bx, by)
            if cte > 120.0:
                continue
            observed = True
            peak_speed = max(peak_speed, math.hypot(_float(row, 'u'), _float(row, 'v')))

    return {
        'post_turn_recovery_observed': observed,
        'min_post_turn_recovery_peak_speed_mps': peak_speed,
    }


def compute_metrics(scenario_path, csv_path):
    scenario = _load_yaml(scenario_path)
    rows = _read_csv(csv_path)
    if not rows:
        raise ValueError(f'CSV contains no samples: {csv_path}')

    waypoints = _analysis_waypoints(scenario)
    if len(waypoints) < 2:
        raise ValueError('scenario own_ship.waypoints must contain at least two points')

    cross_track_errors = []
    heading_errors = []
    yaw_rates = []
    speeds = []
    transit_speeds = []
    final_distances = []
    arrival_radius = float(
        scenario.get('expected', {}).get(
            'arrival_radius_m',
            scenario.get('own_ship', {}).get('arrival_radius_m', 20.0),
        )
    )
    arrival_time_s = None
    target_speed_mps = float(
        scenario.get('expected', {}).get(
            'target_speed_mps',
            scenario.get('own_ship', {}).get('nominal_speed_mps', 0.0),
        )
    )
    final_wp_x, final_wp_y = map(float, waypoints[-1])
    validation = scenario.get('validation', {})
    corner_heading_radius_m = float(
        validation.get('corner_heading_transition_radius_m', 0.0)
    )
    heading_min_forward_speed_mps = float(
        validation.get('heading_min_forward_speed_mps', 0.0)
    )
    course_over_ground_min_forward_speed_mps = float(
        validation.get('course_over_ground_min_forward_speed_mps', 0.1)
    )
    heading_sample_count = 0
    heading_skipped_low_speed_count = 0
    turn_oscillation_heading_error_deg = float(
        validation.get('turn_oscillation_heading_error_deg', 20.0)
    )
    turn_oscillation_yaw_rate_deg_s = float(
        validation.get('turn_oscillation_yaw_rate_deg_s', 3.0)
    )
    turn_transition_heading_errors = []
    turn_transition_yaw_rates = []
    turn_oscillation_violation_count = 0

    for row in rows:
        cte, route_heading = _route_error(row, waypoints)
        x = _float(row, 'x')
        y = _float(row, 'y')
        psi_deg = _float(row, 'psi_deg')
        u = _float(row, 'u')
        v = _float(row, 'v')
        r = _float(row, 'r')
        final_distance = math.hypot(x - final_wp_x, y - final_wp_y)
        in_transit = arrival_time_s is None
        if in_transit:
            speed = math.hypot(u, v)
            transit_speeds.append(speed)
            cross_track_errors.append(cte)
            if u <= heading_min_forward_speed_mps:
                heading_skipped_low_speed_count += 1
            else:
                track_angle_deg = psi_deg
                if u > course_over_ground_min_forward_speed_mps:
                    track_angle_deg = psi_deg + math.degrees(math.atan2(v, u))
                heading_candidates = [route_heading]
                heading_candidates.extend(
                    _corner_heading_candidates(x, y, waypoints, corner_heading_radius_m)
                )
                heading_error_candidates = [
                    abs(_wrap_deg(track_angle_deg - heading)) for heading in heading_candidates
                ]
                heading_error_candidates.extend(
                    _corner_transition_heading_errors(
                        x, y, track_angle_deg, waypoints, corner_heading_radius_m
                    )
                )
                transition_errors = _corner_transition_heading_errors(
                    x, y, track_angle_deg, waypoints, corner_heading_radius_m
                )
                if transition_errors:
                    transition_error = min(transition_errors)
                    yaw_rate_deg_s = abs(math.degrees(r))
                    turn_transition_heading_errors.append(transition_error)
                    turn_transition_yaw_rates.append(yaw_rate_deg_s)
                    if (
                        transition_error > turn_oscillation_heading_error_deg
                        or yaw_rate_deg_s > turn_oscillation_yaw_rate_deg_s
                    ):
                        turn_oscillation_violation_count += 1
                heading_errors.append(min(heading_error_candidates))
                heading_sample_count += 1
            yaw_rates.append(abs(math.degrees(r)))
        speeds.append(math.hypot(u, v))
        final_distances.append(final_distance)
        if arrival_time_s is None and final_distance <= arrival_radius:
            arrival_time_s = _float(row, 'time')

    last = rows[-1]
    final_x = _float(last, 'x')
    final_y = _float(last, 'y')
    final_position_error = math.hypot(final_x - final_wp_x, final_y - final_wp_y)
    min_final_position_error = min(final_distances)

    metrics = {
        'max_cross_track_error_m': max(cross_track_errors or [0.0]),
        'max_heading_error_deg': max(heading_errors or [0.0]),
        'max_yaw_rate_deg_s': max(yaw_rates or [0.0]),
        'max_final_position_error_m': final_position_error,
        'min_final_position_error_m': min_final_position_error,
        'max_final_speed_mps': speeds[-1],
        'target_speed_mps': target_speed_mps,
        'max_speed_mps': max(transit_speeds or speeds or [0.0]),
        'min_peak_speed_mps': max(transit_speeds or speeds or [0.0]),
        'min_mean_transit_speed_mps': (
            sum(transit_speeds) / len(transit_speeds)
            if transit_speeds else 0.0
        ),
        'max_speed_overshoot_mps': max(
            0.0,
            max(transit_speeds or speeds or [0.0]) - target_speed_mps,
        ),
        'max_arrival_time_s': 1e9 if arrival_time_s is None else arrival_time_s,
        'min_forward_speed_mps': min(_float(row, 'u') for row in rows),
        'arrival_radius_m': arrival_radius,
        'final_waypoint_reached': min_final_position_error <= arrival_radius,
        'arrival_time_s': -1.0 if arrival_time_s is None else arrival_time_s,
        'heading_min_forward_speed_mps': heading_min_forward_speed_mps,
        'course_over_ground_min_forward_speed_mps': course_over_ground_min_forward_speed_mps,
        'heading_sample_count': heading_sample_count,
        'heading_skipped_low_speed_count': heading_skipped_low_speed_count,
        'max_turn_transition_heading_error_deg': max(
            turn_transition_heading_errors or [0.0]
        ),
        'max_turn_transition_yaw_rate_deg_s': max(
            turn_transition_yaw_rates or [0.0]
        ),
        'turn_oscillation_violation_count': turn_oscillation_violation_count,
        'should_not_oscillate_after_turn': turn_oscillation_violation_count == 0,
    }
    metrics.update(_side_coverage_metrics(rows, waypoints))
    metrics.update(_waypoint_coverage_metrics(rows, waypoints))
    metrics.update(_route_plan_xte_metrics(rows, waypoints, scenario))
    metrics.update(_post_turn_recovery_metrics(rows, scenario))

    restricted = scenario.get('environment', {}).get('restricted_water', {})
    if 'channel_half_width_m' in restricted:
        half_width = float(restricted['channel_half_width_m'])
        metrics['min_channel_margin_m'] = half_width - max(cross_track_errors)

    return scenario.get('scenario_id', Path(scenario_path).stem), metrics


def main(argv=None):
    parser = argparse.ArgumentParser(description='Compute scenario metrics from ship_dynamics CSV output.')
    parser.add_argument('--scenario', required=True, help='Scenario YAML file.')
    parser.add_argument('--csv', required=True, help='ship_dynamics CSV log.')
    parser.add_argument('--output', help='Write metrics YAML. Defaults to stdout.')
    args = parser.parse_args(argv)

    scenario_id, metrics = compute_metrics(Path(args.scenario), Path(args.csv))
    report = {'scenarios': {scenario_id: metrics}}
    text = yaml.safe_dump(report, sort_keys=True)
    if args.output:
        with open(args.output, 'w', encoding='utf-8') as stream:
            stream.write(text)
    else:
        print(text, end='')


if __name__ == '__main__':
    main(sys.argv[1:])
