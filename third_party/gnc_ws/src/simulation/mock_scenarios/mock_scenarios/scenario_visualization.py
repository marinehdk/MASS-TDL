from pathlib import Path
import argparse
import csv
import html
import json
import math
import sys

import yaml

from mock_scenarios.route_execution_plan import build_route_execution_plan


def _load_yaml(path):
    if not path:
        return {}
    with open(path, 'r', encoding='utf-8') as stream:
        return yaml.safe_load(stream) or {}


def _load_json(path):
    if not path:
        return []
    with open(path, 'r', encoding='utf-8') as stream:
        return json.load(stream) or []


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


def _route_chord_offset(x, y, waypoints):
    sx, sy = map(float, waypoints[0])
    ex, ey = map(float, waypoints[-1])
    dx = ex - sx
    dy = ey - sy
    length = math.hypot(dx, dy)
    if length < 1e-9:
        return 0.0
    return (dx * (float(y) - sy) - dy * (float(x) - sx)) / length


def _latest(pattern, root):
    matches = sorted(Path(root).glob(pattern), key=lambda path: path.stat().st_mtime)
    return matches[-1] if matches else None


def _downsample(points, max_points=1400):
    if len(points) <= max_points:
        return points
    step = max(1, math.ceil(len(points) / max_points))
    sampled = points[::step]
    if sampled[-1] != points[-1]:
        sampled.append(points[-1])
    return sampled


def _fmt(value, digits=4):
    if value is None:
        return ''
    if isinstance(value, bool):
        return str(value).lower()
    if isinstance(value, (int, float)):
        return f'{value:.{digits}f}'.rstrip('0').rstrip('.')
    if isinstance(value, list):
        return ', '.join(str(item) for item in value)
    return str(value)


def _polyline(points):
    return ' '.join(f'{x:.2f},{y:.2f}' for x, y in points)


def _series_svg(title, series, y_label='', limit=None, width=980, height=230):
    margin = {'left': 62, 'right': 20, 'top': 28, 'bottom': 34}
    plot_w = width - margin['left'] - margin['right']
    plot_h = height - margin['top'] - margin['bottom']
    values = [(float(x), float(y)) for x, y in series if math.isfinite(float(y))]
    if not values:
        return f'<section class="panel"><h2>{html.escape(title)}</h2><p>No samples.</p></section>'

    xs = [point[0] for point in values]
    ys = [point[1] for point in values]
    y_candidates = ys + ([float(limit)] if limit is not None else [])
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(y_candidates), max(y_candidates)
    if abs(max_x - min_x) < 1e-9:
        max_x += 1.0
    if abs(max_y - min_y) < 1e-9:
        min_y -= 1.0
        max_y += 1.0
    y_pad = (max_y - min_y) * 0.08
    min_y -= y_pad
    max_y += y_pad

    def sx(value):
        return margin['left'] + (value - min_x) / (max_x - min_x) * plot_w

    def sy(value):
        return margin['top'] + (max_y - value) / (max_y - min_y) * plot_h

    sampled = _downsample(values)
    path = _polyline((sx(x), sy(y)) for x, y in sampled)
    limit_line = ''
    if limit is not None:
        ly = sy(float(limit))
        limit_line = (
            f'<line class="limit" x1="{margin["left"]}" y1="{ly:.2f}" '
            f'x2="{width - margin["right"]}" y2="{ly:.2f}" />'
            f'<text class="limit-label" x="{width - margin["right"] - 100}" y="{ly - 6:.2f}">'
            f'limit {html.escape(_fmt(limit))}</text>'
        )

    ticks = []
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        tx = min_x + (max_x - min_x) * frac
        px = sx(tx)
        ticks.append(
            f'<line class="grid" x1="{px:.2f}" y1="{margin["top"]}" '
            f'x2="{px:.2f}" y2="{height - margin["bottom"]}" />'
        )
        ticks.append(
            f'<text class="tick" x="{px:.2f}" y="{height - 10}" text-anchor="middle">'
            f'{html.escape(_fmt(tx, 0))}</text>'
        )
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        ty = min_y + (max_y - min_y) * frac
        py = sy(ty)
        ticks.append(
            f'<line class="grid" x1="{margin["left"]}" y1="{py:.2f}" '
            f'x2="{width - margin["right"]}" y2="{py:.2f}" />'
        )
        ticks.append(
            f'<text class="tick" x="{margin["left"] - 8}" y="{py + 4:.2f}" text-anchor="end">'
            f'{html.escape(_fmt(ty, 2))}</text>'
        )

    return f'''
<section class="panel">
  <h2>{html.escape(title)}</h2>
  <svg class="chart" viewBox="0 0 {width} {height}" role="img" aria-label="{html.escape(title)}">
    <rect class="plot-bg" x="{margin['left']}" y="{margin['top']}" width="{plot_w}" height="{plot_h}" />
    {''.join(ticks)}
    {limit_line}
    <polyline class="series" points="{path}" />
    <text class="axis" x="{width / 2:.1f}" y="{height - 3}" text-anchor="middle">time (s)</text>
    <text class="axis" x="18" y="{height / 2:.1f}" transform="rotate(-90 18 {height / 2:.1f})" text-anchor="middle">{html.escape(y_label)}</text>
  </svg>
</section>
'''


def _captain_visible_gates(scenario):
    route_plan = scenario.get('route_plan', {}) or {}
    gates = route_plan.get('captain_visible_gates') or []
    points = []
    for gate in gates:
        xy = gate.get('local_xy_m') if isinstance(gate, dict) else None
        if isinstance(xy, list) and len(xy) == 2:
            points.append({
                'id': gate.get('id', ''),
                'role': gate.get('role', ''),
                'xy': (float(xy[0]), float(xy[1])),
            })
    return points


def _trajectory_svg(rows, waypoints, scenario, width=980, height=520):
    margin = 34
    xy = [(_float(row, 'x'), _float(row, 'y')) for row in rows]
    internal_route = [(float(x), float(y)) for x, y in waypoints]
    captain_gates = _captain_visible_gates(scenario)
    captain_route = [gate['xy'] for gate in captain_gates]
    all_points = xy + internal_route + captain_route
    min_x = min(point[0] for point in all_points)
    max_x = max(point[0] for point in all_points)
    min_y = min(point[1] for point in all_points)
    max_y = max(point[1] for point in all_points)
    if abs(max_x - min_x) < 1e-9:
        max_x += 1.0
    if abs(max_y - min_y) < 1e-9:
        min_y -= 1.0
        max_y += 1.0
    dx = max_x - min_x
    dy = max_y - min_y
    min_x -= dx * 0.04
    max_x += dx * 0.04
    min_y -= dy * 0.12
    max_y += dy * 0.12

    def sx(value):
        return margin + (value - min_x) / (max_x - min_x) * (width - 2 * margin)

    def sy(value):
        return height - margin - (value - min_y) / (max_y - min_y) * (height - 2 * margin)

    path = _polyline((sx(x), sy(y)) for x, y in _downsample(xy, 2200))
    internal_route_points = _polyline((sx(x), sy(y)) for x, y in internal_route)
    captain_route_points = _polyline((sx(x), sy(y)) for x, y in captain_route)
    has_captain_gates = len(captain_route) >= 2
    visualization_cfg = (scenario.get('route_plan', {}) or {}).get('visualization', {}) or {}
    route_execution = build_route_execution_plan(scenario)
    route_execution_enabled = bool(route_execution.get('enabled'))
    hide_internal_labels = bool(
        visualization_cfg.get('hide_internal_waypoint_labels', has_captain_gates)
    )
    mark_stride = max(1, math.ceil(len(internal_route) / 70)) if route_execution_enabled else 1
    waypoint_marks = []
    for index, (x, y) in enumerate(internal_route):
        if route_execution_enabled and index not in {0, len(internal_route) - 1} and index % mark_stride != 0:
            continue
        px = sx(x)
        py = sy(y)
        label = '' if hide_internal_labels else (
            f'<text class="wp-label" x="{px + 8:.2f}" y="{py - 8:.2f}">wp{index}</text>'
        )
        waypoint_marks.append(
            f'<circle class="internal-waypoint" cx="{px:.2f}" cy="{py:.2f}" r="3.6">'
            f'<title>internal wp{index}</title></circle>{label}'
        )
    captain_marks = []
    for index, gate in enumerate(captain_gates):
        x, y = gate['xy']
        px = sx(x)
        py = sy(y)
        label = gate['id'] or f'gate{index}'
        captain_marks.append(
            f'<circle class="captain-gate" cx="{px:.2f}" cy="{py:.2f}" r="6.4">'
            f'<title>{html.escape(gate["role"] or label)}</title></circle>'
            f'<text class="captain-label" x="{px + 9:.2f}" y="{py - 9:.2f}">{html.escape(label)}</text>'
        )
    start_x, start_y = sx(xy[0][0]), sy(xy[0][1])
    end_x, end_y = sx(xy[-1][0]), sy(xy[-1][1])
    route_layers = (
        f'<polyline class="route-internal" points="{internal_route_points}" />'
        if has_captain_gates
        else f'<polyline class="route" points="{internal_route_points}" />'
    )
    if has_captain_gates:
        route_layers += f'<polyline class="captain-route" points="{captain_route_points}" />'
    legend = (
        '<g class="legend" transform="translate(48 56)">'
        '<line class="track" x1="0" y1="0" x2="28" y2="0" />'
        '<text x="36" y="4">actual track</text>'
        '<line class="captain-route" x1="145" y1="0" x2="173" y2="0" />'
        '<text x="181" y="4">captain gates</text>'
        '<line class="route-internal" x1="310" y1="0" x2="338" y2="0" />'
        '<text x="346" y="4">route execution</text>'
        '</g>'
        if has_captain_gates
        else ''
    )
    return f'''
<section class="panel wide">
  <h2>Trajectory vs Planned Route</h2>
  <svg class="trajectory" viewBox="0 0 {width} {height}" role="img" aria-label="Trajectory">
    <rect class="plot-bg" x="{margin}" y="{margin}" width="{width - 2 * margin}" height="{height - 2 * margin}" />
    {route_layers}
    <polyline class="track" points="{path}" />
    {''.join(waypoint_marks)}
    {''.join(captain_marks)}
    <circle class="start" cx="{start_x:.2f}" cy="{start_y:.2f}" r="6" />
    <circle class="finish" cx="{end_x:.2f}" cy="{end_y:.2f}" r="6" />
    {legend}
  </svg>
</section>
'''


def _table(title, rows, columns):
    body = []
    for row in rows:
        cells = ''.join(f'<td>{html.escape(_fmt(row.get(key)))}</td>' for key, _label in columns)
        body.append(f'<tr>{cells}</tr>')
    heads = ''.join(f'<th>{html.escape(label)}</th>' for _key, label in columns)
    return f'''
<section class="panel">
  <h2>{html.escape(title)}</h2>
  <table>
    <thead><tr>{heads}</tr></thead>
    <tbody>{''.join(body)}</tbody>
  </table>
</section>
'''


def build_html(scenario_path, csv_path, metrics_path=None, acceptance_path=None):
    scenario = _load_yaml(scenario_path)
    rows = _read_csv(csv_path)
    if not rows:
        raise ValueError(f'CSV contains no samples: {csv_path}')
    route_execution = build_route_execution_plan(scenario)
    waypoints = route_execution.get('guidance_waypoints') or scenario.get('own_ship', {}).get('waypoints') or []
    if len(waypoints) < 2:
        raise ValueError('scenario own_ship.waypoints must contain at least two points')

    scenario_id = scenario.get('scenario_id', Path(scenario_path).stem)
    metrics_raw = _load_yaml(metrics_path)
    metrics = metrics_raw.get('scenarios', {}).get(scenario_id, {}) if metrics_raw else {}
    acceptance_rows = _load_json(acceptance_path)

    final_x, final_y = map(float, waypoints[-1])
    speed = []
    yaw_rate = []
    cte = []
    heading_error = []
    final_distance = []
    lateral_offset = []
    for row in rows:
        t = _float(row, 'time')
        u = _float(row, 'u')
        v = _float(row, 'v')
        psi_deg = _float(row, 'psi_deg')
        route_distance, route_heading = _route_error(row, waypoints)
        speed.append((t, math.hypot(u, v)))
        yaw_rate.append((t, abs(math.degrees(_float(row, 'r')))))
        cte.append((t, route_distance))
        track_angle = psi_deg
        if u > 0.5:
            track_angle = psi_deg + math.degrees(math.atan2(v, u))
        heading_error.append((t, abs(_wrap_deg(track_angle - route_heading))))
        final_distance.append((t, math.hypot(_float(row, 'x') - final_x, _float(row, 'y') - final_y)))
        lateral_offset.append((t, _route_chord_offset(_float(row, 'x'), _float(row, 'y'), waypoints)))

    expected = scenario.get('expected', {}) or {}
    metric_rows = [{'metric': key, 'value': value} for key, value in metrics.items()]
    acceptance_table = acceptance_rows if isinstance(acceptance_rows, list) else []

    html_doc = f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<title>{html.escape(scenario_id)} visualization</title>
<style>
  :root {{
    color-scheme: light;
    --ink: #1b2533;
    --muted: #5b6675;
    --line: #d8dee8;
    --panel: #ffffff;
    --bg: #f4f6f8;
    --accent: #0f766e;
    --route: #475569;
    --limit: #b91c1c;
  }}
  body {{
    margin: 0;
    font-family: Arial, Helvetica, sans-serif;
    background: var(--bg);
    color: var(--ink);
  }}
  header {{
    padding: 28px 34px 18px;
    background: #102033;
    color: white;
  }}
  header h1 {{
    margin: 0 0 8px;
    font-size: 26px;
    letter-spacing: 0;
  }}
  header p {{
    margin: 4px 0;
    color: #dbe7f3;
  }}
  main {{
    padding: 22px 26px 36px;
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(520px, 1fr));
    gap: 18px;
  }}
  .panel {{
    background: var(--panel);
    border: 1px solid var(--line);
    border-radius: 8px;
    padding: 16px;
    box-shadow: 0 1px 2px rgba(15, 23, 42, 0.04);
  }}
  .wide {{
    grid-column: 1 / -1;
  }}
  h2 {{
    margin: 0 0 12px;
    font-size: 17px;
    letter-spacing: 0;
  }}
  .chart, .trajectory {{
    width: 100%;
    height: auto;
  }}
  .plot-bg {{
    fill: #fbfcfe;
    stroke: var(--line);
  }}
  .grid {{
    stroke: #e8edf3;
    stroke-width: 1;
  }}
  .series {{
    fill: none;
    stroke: var(--accent);
    stroke-width: 2.2;
  }}
  .limit {{
    stroke: var(--limit);
    stroke-width: 1.4;
    stroke-dasharray: 6 5;
  }}
  .limit-label, .tick, .axis, .wp-label {{
    fill: var(--muted);
    font-size: 12px;
  }}
  .route {{
    fill: none;
    stroke: var(--route);
    stroke-width: 2;
    stroke-dasharray: 9 6;
  }}
  .route-internal {{
    fill: none;
    stroke: #94a3b8;
    stroke-width: 1.6;
    stroke-dasharray: 5 7;
  }}
  .captain-route {{
    fill: none;
    stroke: #2563eb;
    stroke-width: 2.2;
    stroke-dasharray: 12 5;
  }}
  .track {{
    fill: none;
    stroke: var(--accent);
    stroke-width: 2.4;
  }}
  .waypoint {{
    fill: white;
    stroke: var(--route);
    stroke-width: 2;
  }}
  .internal-waypoint {{
    fill: white;
    stroke: #94a3b8;
    stroke-width: 1.6;
  }}
  .captain-gate {{
    fill: #eff6ff;
    stroke: #2563eb;
    stroke-width: 2.4;
  }}
  .captain-label {{
    fill: #1e3a8a;
    font-size: 12px;
    font-weight: 700;
  }}
  .legend text {{
    fill: var(--muted);
    font-size: 12px;
  }}
  .start {{
    fill: #2563eb;
  }}
  .finish {{
    fill: #dc2626;
  }}
  table {{
    width: 100%;
    border-collapse: collapse;
    font-size: 13px;
  }}
  th, td {{
    border-bottom: 1px solid var(--line);
    padding: 7px 8px;
    text-align: left;
    vertical-align: top;
  }}
  th {{
    color: var(--muted);
    font-weight: 700;
  }}
</style>
</head>
<body>
<header>
  <h1>{html.escape(scenario_id)}</h1>
  <p>{html.escape(scenario.get('description', ''))}</p>
  <p>CSV: {html.escape(str(csv_path))}</p>
  <p>Scenario: {html.escape(str(scenario_path))}</p>
</header>
<main>
  {_trajectory_svg(rows, waypoints, scenario)}
  {_series_svg('Speed', speed, 'm/s')}
  {_series_svg('Cross Track Error', cte, 'm', expected.get('max_cross_track_error_m'))}
  {_series_svg('Route-Side Lateral Offset', lateral_offset, 'm')}
  {_series_svg('Heading Error', heading_error, 'deg', expected.get('max_heading_error_deg'))}
  {_series_svg('Yaw Rate', yaw_rate, 'deg/s', expected.get('max_yaw_rate_deg_s'))}
  {_series_svg('Final Waypoint Distance', final_distance, 'm', metrics.get('arrival_radius_m'))}
  {_table('Merged Metrics', metric_rows, [('metric', 'Metric'), ('value', 'Observed')])}
  {_table('Acceptance', acceptance_table, [('status', 'Status'), ('metric', 'Metric'), ('expected', 'Expected'), ('observed', 'Observed'), ('detail', 'Detail')]) if acceptance_table else ''}
</main>
</body>
</html>
'''
    return html_doc


def main(argv=None):
    parser = argparse.ArgumentParser(description='Generate a self-contained HTML visualization for one scenario run.')
    parser.add_argument('--scenario', required=True, help='Scenario YAML file.')
    parser.add_argument('--run-dir', help='Run directory containing CSV, metrics, and acceptance files.')
    parser.add_argument('--csv', help='ship_dynamics CSV. Defaults to latest ship_sim_*.csv in --run-dir.')
    parser.add_argument('--metrics', help='metrics YAML. Defaults to metrics_merged.yaml or metrics.yaml in --run-dir.')
    parser.add_argument('--acceptance', help='acceptance JSON. Defaults to acceptance.json in --run-dir.')
    parser.add_argument('--output', help='Output HTML. Defaults to scenario_visualization.html in --run-dir.')
    args = parser.parse_args(argv)

    run_dir = Path(args.run_dir) if args.run_dir else None
    csv_path = Path(args.csv) if args.csv else _latest('ship_sim_*.csv', run_dir)
    if not csv_path:
        raise SystemExit('CSV not found; pass --csv or --run-dir with ship_sim_*.csv')
    metrics_path = Path(args.metrics) if args.metrics else None
    if not metrics_path and run_dir:
        metrics_path = run_dir / 'metrics_merged.yaml'
        if not metrics_path.exists():
            metrics_path = run_dir / 'metrics.yaml'
    acceptance_path = Path(args.acceptance) if args.acceptance else None
    if not acceptance_path and run_dir:
        acceptance_path = run_dir / 'acceptance.json'
    output_path = Path(args.output) if args.output else None
    if not output_path:
        if not run_dir:
            raise SystemExit('Output not specified; pass --output or --run-dir')
        output_path = run_dir / 'scenario_visualization.html'

    html_doc = build_html(
        Path(args.scenario),
        csv_path,
        metrics_path if metrics_path and metrics_path.exists() else None,
        acceptance_path if acceptance_path and acceptance_path.exists() else None,
    )
    with open(output_path, 'w', encoding='utf-8') as stream:
        stream.write(html_doc)
    print(output_path)


if __name__ == '__main__':
    main(sys.argv[1:])
