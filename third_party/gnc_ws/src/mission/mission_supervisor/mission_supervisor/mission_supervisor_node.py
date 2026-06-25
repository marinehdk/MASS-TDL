import json
import math
from pathlib import Path

import rclpy
import yaml
from nav_msgs.msg import Odometry
from rclpy.node import Node
from rclpy.parameter import Parameter
from std_msgs.msg import Bool, String


PHASES = (
    'PRECHECK',
    'CRUISE',
    'DECEL',
    'REPORT',
    'APPROACH',
    'STANDBY',
    'BERTH_OR_WORK',
    'ABORT_ESCAPE',
    'COMPLETE',
)


def _load_yaml(path):
    if not path:
        return {}
    p = Path(path)
    if not p.exists():
        return {}
    with p.open('r', encoding='utf-8') as stream:
        return yaml.safe_load(stream) or {}


def _as_bool(value):
    return str(value).strip().lower() in {'1', 'true', 'yes', 'on'}


def _yaw_from_quaternion(q):
    siny_cosp = 2.0 * (q.w * q.z + q.x * q.y)
    cosy_cosp = 1.0 - 2.0 * (q.y * q.y + q.z * q.z)
    return math.atan2(siny_cosp, cosy_cosp)


def _distance(left, right):
    return math.hypot(float(left[0]) - float(right[0]), float(left[1]) - float(right[1]))


def _route_lengths(waypoints):
    cumulative = [0.0]
    total = 0.0
    for idx in range(len(waypoints) - 1):
        total += _distance(waypoints[idx], waypoints[idx + 1])
        cumulative.append(total)
    return cumulative, total


def _route_position(point, waypoints):
    if not waypoints:
        return {
            'progress_m': 0.0,
            'route_length_m': 0.0,
            'cross_track_error_m': 0.0,
            'distance_to_final_m': 0.0,
            'final_position_error_m': 0.0,
            'segment_index': 0,
        }
    if len(waypoints) == 1:
        d = _distance(point, waypoints[0])
        return {
            'progress_m': 0.0,
            'route_length_m': 0.0,
            'cross_track_error_m': d,
            'distance_to_final_m': d,
            'final_position_error_m': d,
            'segment_index': 0,
        }

    cumulative, total = _route_lengths(waypoints)
    px, py = point
    best = None
    for idx in range(len(waypoints) - 1):
        ax, ay = waypoints[idx]
        bx, by = waypoints[idx + 1]
        vx = bx - ax
        vy = by - ay
        wx = px - ax
        wy = py - ay
        seg_len_sq = vx * vx + vy * vy
        t = 0.0 if seg_len_sq <= 1e-9 else max(0.0, min(1.0, (wx * vx + wy * vy) / seg_len_sq))
        proj_x = ax + t * vx
        proj_y = ay + t * vy
        cross = math.hypot(px - proj_x, py - proj_y)
        progress = cumulative[idx] + math.sqrt(seg_len_sq) * t
        candidate = (cross, progress, idx)
        if best is None or candidate[0] < best[0]:
            best = candidate

    cross, progress, segment_index = best
    final_error = _distance(point, waypoints[-1])
    return {
        'progress_m': progress,
        'route_length_m': total,
        'cross_track_error_m': cross,
        'distance_to_final_m': max(0.0, total - progress),
        'final_position_error_m': final_error,
        'segment_index': segment_index,
    }


class MissionSupervisorNode(Node):
    """Shadow-mode hierarchical mission state machine."""

    def __init__(self):
        super().__init__('mission_supervisor_node')
        self.declare_parameter('config_file', '')
        self.declare_parameter('scenario_file', '')
        self.declare_parameter('shadow_mode', True)
        self.declare_parameter('publish_rate_hz', 2.0)
        self.declare_parameter('auto_advance', True)
        self.declare_parameter('route_wp_x', Parameter.Type.DOUBLE_ARRAY)
        self.declare_parameter('route_wp_y', Parameter.Type.DOUBLE_ARRAY)
        self.declare_parameter('route_xte_limits_m', Parameter.Type.DOUBLE_ARRAY)

        self.config_file = str(self.get_parameter('config_file').value or '')
        self.scenario_file = str(self.get_parameter('scenario_file').value or '')
        self.config = _load_yaml(self.config_file)
        self.scenario = _load_yaml(self.scenario_file)
        self.shadow_mode = _as_bool(self.get_parameter('shadow_mode').value)
        self.auto_advance = _as_bool(self.get_parameter('auto_advance').value)
        self.data_policy = self.config.get('data_policy', {}) or {}
        self.gates = self.config.get('gates', {}) or {}
        self.permissions = self.config.get('permissions', {}) or {}
        self.safety_cfg = self.config.get('safety', {}) or {}

        config_rate = float(self.config.get('publish_rate_hz', 2.0))
        rate = float(self.get_parameter('publish_rate_hz').value or config_rate)
        self.period_s = 1.0 / max(rate, 0.1)

        self.phase = 'PRECHECK'
        self.previous_phase = ''
        self.phase_enter_time = self.get_clock().now()
        self.start_time = self.get_clock().now()
        self.first_odom_time = None
        self.last_odom_time = None
        self.odom = None
        self.safety_abort = False
        self.safety_status = {}

        self.scenario_id = self.scenario.get('scenario_id', Path(self.scenario_file).stem or 'unknown')
        self.route_wp_x = [float(value) for value in self.get_parameter('route_wp_x').value]
        self.route_wp_y = [float(value) for value in self.get_parameter('route_wp_y').value]
        self.route_xte_limits_m = [float(value) for value in self.get_parameter('route_xte_limits_m').value]
        self.waypoints = self._scenario_waypoints()
        self.arrival_radius_m = float(
            self.scenario.get('expected', {}).get(
                'arrival_radius_m',
                self.scenario.get('own_ship', {}).get(
                    'arrival_radius_m',
                    self.gates.get('arrival_radius_m_default', 20.0),
                ),
            )
        )

        self.create_subscription(Odometry, '/ship/odometry', self._on_odom, 20)
        self.create_subscription(Bool, '/safety/abort_request', self._on_safety_abort, 10)
        self.create_subscription(String, '/safety/status', self._on_safety_status, 10)

        self.phase_pub = self.create_publisher(String, '/mission/phase', 10)
        self.active_gate_pub = self.create_publisher(String, '/mission/active_gate', 10)
        self.status_pub = self.create_publisher(String, '/mission/status', 10)

        self.timer = self.create_timer(self.period_s, self._tick)
        self.get_logger().info(
            f'MissionSupervisor shadow_mode={self.shadow_mode} scenario={self.scenario_id} '
            f'config={self.config_file or "<defaults>"}'
        )

    def _scenario_waypoints(self):
        if len(self.route_wp_x) >= 2 and len(self.route_wp_x) == len(self.route_wp_y):
            return list(zip(self.route_wp_x, self.route_wp_y))

        own_ship = self.scenario.get('own_ship', {}) or {}
        waypoints = own_ship.get('waypoints') or []
        result = []
        for item in waypoints:
            if len(item) >= 2:
                result.append((float(item[0]), float(item[1])))
        return result

    def _on_odom(self, msg):
        self.odom = msg
        now = self.get_clock().now()
        self.last_odom_time = now
        if self.first_odom_time is None:
            self.first_odom_time = now

    def _on_safety_abort(self, msg):
        self.safety_abort = bool(msg.data)

    def _on_safety_status(self, msg):
        try:
            self.safety_status = json.loads(msg.data) if msg.data else {}
        except json.JSONDecodeError:
            self.safety_status = {'raw': msg.data}

    def _elapsed_since(self, stamp):
        if stamp is None:
            return 0.0
        return (self.get_clock().now() - stamp).nanoseconds * 1e-9

    def _odom_stale(self):
        if self.last_odom_time is None:
            grace = float(self.config.get('startup_grace_s', 0.0))
            return self._elapsed_since(self.start_time) > grace
        stale_cfg = self.config.get('stale_timeout_s', {}) or {}
        return self._elapsed_since(self.last_odom_time) > float(stale_cfg.get('odometry', 2.0))

    def _ownship(self):
        if self.odom is None:
            return {
                'position': (0.0, 0.0),
                'yaw_deg': 0.0,
                'speed_mps': 0.0,
                'yaw_rate_deg_s': 0.0,
                'odom_available': False,
            }
        pose = self.odom.pose.pose
        twist = self.odom.twist.twist
        yaw = _yaw_from_quaternion(pose.orientation)
        return {
            'position': (float(pose.position.x), float(pose.position.y)),
            'yaw_deg': math.degrees(yaw),
            'speed_mps': math.hypot(float(twist.linear.x), float(twist.linear.y)),
            'yaw_rate_deg_s': abs(math.degrees(float(twist.angular.z))),
            'odom_available': True,
        }

    def _nav_report(self):
        ownship = self._ownship()
        route = _route_position(ownship['position'], self.waypoints)
        segment_index = int(route.get('segment_index', 0))
        if 0 <= segment_index < len(self.route_xte_limits_m):
            route['route_xte_limit_m'] = self.route_xte_limits_m[segment_index]
        else:
            route['route_xte_limit_m'] = 0.0
        route.update(ownship)
        route['odom_stale'] = self._odom_stale()
        route['arrival_radius_m'] = self.arrival_radius_m
        return route

    def _phase_dwell_s(self):
        return self._elapsed_since(self.phase_enter_time)

    def _set_phase(self, phase):
        if phase == self.phase or phase not in PHASES:
            return
        self.previous_phase = self.phase
        self.phase = phase
        self.phase_enter_time = self.get_clock().now()

    def _gate(self, name, passed, reasons, nav):
        return {
            'name': name,
            'phase': self.phase,
            'passed': bool(passed),
            'reasons': reasons,
            'distance_to_final_m': nav['distance_to_final_m'],
            'final_position_error_m': nav['final_position_error_m'],
            'cross_track_error_m': nav['cross_track_error_m'],
            'route_xte_limit_m': nav.get('route_xte_limit_m', 0.0),
            'speed_mps': nav['speed_mps'],
        }

    def _active_gate(self, nav):
        if self.phase == 'PRECHECK':
            reasons = []
            if not nav['odom_available']:
                reasons.append('odometry_missing')
            if self.first_odom_time is not None and self._elapsed_since(self.first_odom_time) < float(self.gates.get('precheck_min_observation_s', 0.5)):
                reasons.append('minimum_observation_time_not_met')
            return self._gate('precheck', not reasons, reasons, nav)

        if self.phase == 'CRUISE':
            limit = float(self.gates.get('decel_distance_m', 250.0))
            passed = nav['distance_to_final_m'] <= limit
            reasons = [] if passed else [f'distance_to_final_gt_{limit:.1f}m']
            return self._gate('decel_gate', passed, reasons, nav)

        if self.phase == 'DECEL':
            limit = float(self.gates.get('report_distance_m', 120.0))
            passed = nav['distance_to_final_m'] <= limit
            reasons = [] if passed else [f'distance_to_final_gt_{limit:.1f}m']
            return self._gate('report_gate', passed, reasons, nav)

        if self.phase == 'REPORT':
            dwell = self._phase_dwell_s()
            min_dwell = float(self.gates.get('report_min_dwell_s', 1.0))
            clearance_ok = bool(self.permissions.get('mock_vts_clearance', False)) and bool(self.permissions.get('mock_berth_available', False))
            passed = self.auto_advance and clearance_ok and dwell >= min_dwell
            reasons = []
            if not self.auto_advance:
                reasons.append('auto_advance_disabled')
            if not clearance_ok:
                reasons.append('mock_clearance_missing')
            if dwell < min_dwell:
                reasons.append('report_dwell_not_met')
            return self._gate('clearance_gate', passed, reasons, nav)

        if self.phase == 'APPROACH':
            limit = float(self.gates.get('standby_radius_m', 35.0))
            speed_limit = float(self.gates.get('standby_speed_mps', 1.2))
            position_ok = nav['final_position_error_m'] <= limit
            speed_ok = nav['speed_mps'] <= speed_limit
            passed = position_ok and speed_ok
            reasons = []
            if not position_ok:
                reasons.append(f'final_position_error_gt_{limit:.1f}m')
            if not speed_ok:
                reasons.append(f'speed_gt_{speed_limit:.2f}mps')
            return self._gate('standby_gate', passed, reasons, nav)

        if self.phase == 'STANDBY':
            dwell = self._phase_dwell_s()
            min_dwell = float(self.gates.get('standby_min_dwell_s', 2.0))
            speed_limit = float(self.gates.get('complete_speed_mps', 0.7))
            position_ok = nav['final_position_error_m'] <= self.arrival_radius_m
            speed_ok = nav['speed_mps'] <= speed_limit
            passed = position_ok and speed_ok and dwell >= min_dwell
            reasons = []
            if not position_ok:
                reasons.append(f'final_position_error_gt_{self.arrival_radius_m:.1f}m')
            if not speed_ok:
                reasons.append(f'speed_gt_{speed_limit:.2f}mps')
            if dwell < min_dwell:
                reasons.append('standby_dwell_not_met')
            return self._gate('complete_gate', passed, reasons, nav)

        if self.phase == 'ABORT_ESCAPE':
            return self._gate('escape_gate', False, ['escape_route_not_configured_in_mock'], nav)

        return self._gate('no_active_gate', True, [], nav)

    def _advance(self, nav, gate):
        if bool(self.safety_cfg.get('abort_on_safety_request', True)) and self.safety_abort and self.phase not in {'ABORT_ESCAPE', 'COMPLETE'}:
            self._set_phase('ABORT_ESCAPE')
            return

        if self.phase == 'PRECHECK' and gate['passed']:
            self._set_phase('CRUISE')
        elif self.phase == 'CRUISE' and gate['passed']:
            self._set_phase('DECEL')
        elif self.phase == 'DECEL' and gate['passed']:
            self._set_phase('REPORT')
        elif self.phase == 'REPORT' and gate['passed']:
            self._set_phase('APPROACH')
        elif self.phase == 'APPROACH' and gate['passed']:
            self._set_phase('STANDBY')
        elif self.phase == 'STANDBY' and gate['passed']:
            final_clearance = bool(self.permissions.get('mock_final_approach_clearance', False))
            self._set_phase('BERTH_OR_WORK' if final_clearance else 'COMPLETE')

    def _status(self, nav, gate):
        return {
            'scenario_id': self.scenario_id,
            'phase': self.phase,
            'previous_phase': self.previous_phase,
            'shadow_mode': self.shadow_mode,
            'parameter_source': self.data_policy.get('parameter_source', 'unknown'),
            'parameter_confidence': self.data_policy.get('parameter_confidence', 'unknown'),
            'waypoint_count': len(self.waypoints),
            'active_gate': gate,
            'progress_m': nav['progress_m'],
            'route_length_m': nav['route_length_m'],
            'distance_to_final_m': nav['distance_to_final_m'],
            'final_position_error_m': nav['final_position_error_m'],
            'cross_track_error_m': nav['cross_track_error_m'],
            'route_xte_limit_m': nav.get('route_xte_limit_m', 0.0),
            'speed_mps': nav['speed_mps'],
            'yaw_deg': nav['yaw_deg'],
            'odom_stale': nav['odom_stale'],
            'safety_abort_request': self.safety_abort,
            'safety_status': self.safety_status.get('status', 'UNKNOWN'),
        }

    def _tick(self):
        nav = self._nav_report()
        gate = self._active_gate(nav)
        self._advance(nav, gate)
        gate = self._active_gate(nav)

        self.phase_pub.publish(String(data=self.phase))
        self.active_gate_pub.publish(String(data=json.dumps(gate, ensure_ascii=True, sort_keys=True)))
        self.status_pub.publish(String(data=json.dumps(self._status(nav, gate), ensure_ascii=True, sort_keys=True)))


def main(args=None):
    rclpy.init(args=args)
    node = MissionSupervisorNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            node.destroy_node()
        except KeyboardInterrupt:
            pass
        if rclpy.ok():
            try:
                rclpy.shutdown()
            except KeyboardInterrupt:
                pass


if __name__ == '__main__':
    main()
