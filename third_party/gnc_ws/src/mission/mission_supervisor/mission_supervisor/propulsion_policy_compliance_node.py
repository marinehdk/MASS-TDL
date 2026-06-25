from pathlib import Path
import json
import math

import rclpy
import yaml
from nav_msgs.msg import Odometry
from rclpy.node import Node
from std_msgs.msg import Float64MultiArray, String


def _load_yaml(path):
    if not path:
        return {}
    p = Path(path)
    if not p.exists():
        return {}
    with p.open('r', encoding='utf-8') as stream:
        return yaml.safe_load(stream) or {}


def _json_or_raw(text):
    if not text:
        return {}
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return {'raw': text}


def _as_bool(value):
    return str(value).strip().lower() in {'1', 'true', 'yes', 'on'}


class PropulsionPolicyComplianceNode(Node):
    """Shadow-mode checker for actual propulsion output vs policy intent."""

    def __init__(self):
        super().__init__('propulsion_policy_compliance_node')
        self.declare_parameter('config_file', '')
        self.declare_parameter('scenario_file', '')
        self.declare_parameter('shadow_mode', True)
        self.declare_parameter('publish_rate_hz', 2.0)

        self.config_file = str(self.get_parameter('config_file').value or '')
        self.scenario_file = str(self.get_parameter('scenario_file').value or '')
        self.config = _load_yaml(self.config_file)
        self.scenario = _load_yaml(self.scenario_file)
        self.shadow_mode = _as_bool(self.get_parameter('shadow_mode').value)
        self.data_policy = self.config.get('data_policy', {}) or {}

        rate = float(self.get_parameter('publish_rate_hz').value or self.config.get('publish_rate_hz', 2.0))
        self.period_s = 1.0 / max(rate, 0.1)

        self.thruster_order = list(self.config.get('thruster_order', []) or [])
        groups = self.config.get('thruster_groups', {}) or {}
        self.main_names = list(groups.get('main_propellers', []) or [])
        self.side_names = list(groups.get('side_thrusters', []) or [])
        self.rudder_names = list(groups.get('rudders', []) or [])
        self.max_thrust_n = {
            str(name): float(value)
            for name, value in (self.config.get('max_thrust_n', {}) or {}).items()
        }

        self.policy = {}
        self.thruster_commands = {}
        self.speed_mps = 0.0

        self.start_time = self.get_clock().now()
        self.last_policy_time = None
        self.last_thruster_time = None
        self.last_odom_time = None

        self.create_subscription(String, '/propulsion/policy', self._on_propulsion_policy, 10)
        self.create_subscription(Float64MultiArray, '/thruster/commands', self._on_thruster_commands, 10)
        self.create_subscription(Odometry, '/ship/odometry', self._on_odometry, 10)

        self.compliance_pub = self.create_publisher(String, '/propulsion/compliance', 10)
        self.metrics_pub = self.create_publisher(Float64MultiArray, '/propulsion/compliance_metrics', 10)

        self.timer = self.create_timer(self.period_s, self._tick)
        self.get_logger().info(
            f'PropulsionPolicyCompliance shadow_mode={self.shadow_mode} config={self.config_file or "<defaults>"}'
        )

    def _on_propulsion_policy(self, msg):
        self.policy = _json_or_raw(msg.data)
        self.last_policy_time = self.get_clock().now()

    def _on_thruster_commands(self, msg):
        data = list(msg.data)
        if not self.thruster_order:
            return
        stride = 3 if len(data) == len(self.thruster_order) * 3 else 2
        if len(data) < len(self.thruster_order) * stride:
            return
        commands = {}
        for index, name in enumerate(self.thruster_order):
            base = index * stride
            commands[name] = {
                'thrust_n': float(data[base]),
                'angle_rad': float(data[base + 1]),
                'bucket': float(data[base + 2]) if stride == 3 else 0.0,
            }
        self.thruster_commands = commands
        self.last_thruster_time = self.get_clock().now()

    def _on_odometry(self, msg):
        linear = msg.twist.twist.linear
        self.speed_mps = math.hypot(float(linear.x), float(linear.y))
        self.last_odom_time = self.get_clock().now()

    def _elapsed_since(self, stamp):
        if stamp is None:
            return float('inf')
        return (self.get_clock().now() - stamp).nanoseconds * 1e-9

    def _is_stale(self, name, stamp):
        timeout = float((self.config.get('stale_timeout_s') or {}).get(name, 5.0))
        return self._elapsed_since(stamp) > timeout

    def _startup_waiting_for_inputs(self, stale):
        missing = [
            name
            for name in ('propulsion_policy', 'thruster_commands', 'odometry')
            if stale.get(name)
        ]
        grace_s = float(self.config.get('initial_observation_grace_s', 0.0) or 0.0)
        if missing and self._elapsed_since(self.start_time) <= grace_s:
            return missing
        return []

    def _commands_for(self, names):
        return [self.thruster_commands.get(name, {'thrust_n': 0.0, 'angle_rad': 0.0}) for name in names]

    def _side_usage(self):
        commands = self._commands_for(self.side_names)
        forces = [abs(cmd['thrust_n']) for cmd in commands]
        total = sum(forces)
        max_force = max(forces) if forces else 0.0
        capacity = sum(float(self.max_thrust_n.get(name, 0.0)) for name in self.side_names)
        return total, max_force, capacity

    def _main_usage(self):
        commands = self._commands_for(self.main_names)
        thrusts = [cmd['thrust_n'] for cmd in commands]
        if not thrusts:
            return 0.0, 0.0, 0.0, False
        max_t = max(thrusts)
        min_t = min(thrusts)
        max_abs = max(abs(item) for item in thrusts)
        asymmetry = max_t - min_t
        reverse_present = any(item < -float((self.config.get('tolerances') or {}).get('reverse_leak_n', 500.0)) for item in thrusts)
        return asymmetry, max_abs, sum(thrusts), reverse_present

    def _thresholds(self):
        return self.config.get('tolerances', {}) or {}

    def _evaluate(self):
        stale = {
            'propulsion_policy': self._is_stale('propulsion_policy', self.last_policy_time),
            'thruster_commands': self._is_stale('thruster_commands', self.last_thruster_time),
            'odometry': self._is_stale('odometry', self.last_odom_time),
        }
        startup_missing = self._startup_waiting_for_inputs(stale)
        violations = []
        warnings = []
        reasons = []

        if startup_missing:
            reasons.extend([f'{name}_initial_observation_pending' for name in startup_missing])
        elif any(stale.values()):
            warnings.extend([f'{name}_stale' for name, is_stale in stale.items() if is_stale])

        constraints = self.policy.get('constraints', {}) if isinstance(self.policy, dict) else {}
        thresholds = self._thresholds()

        side_total, side_max, side_capacity = self._side_usage()
        main_asymmetry, main_max_abs, main_sum, reverse_present = self._main_usage()
        side_allowed = bool(constraints.get('side_thruster_allowed', False))
        side_fraction = float(constraints.get('side_thruster_max_fraction', 0.0) or 0.0)
        main_policy = self.policy.get('main_propulsion_policy', {}) if isinstance(self.policy, dict) else {}
        if not isinstance(main_policy, dict):
            main_policy = {}
        main_symmetry_required = bool(
            main_policy.get('symmetry_required', constraints.get('main_propulsion_symmetry_required', False))
        )
        rudder_preferred = bool(constraints.get('rudder_preferred', False))
        reverse_allowed = bool(constraints.get('reverse_allowed', False))
        operating_region = str(self.policy.get('operating_region', 'unknown'))

        side_leak_n = float(thresholds.get('side_thruster_leak_n', 500.0))
        side_margin = float(thresholds.get('side_thruster_fraction_margin', 0.05))
        if not side_allowed and side_max > side_leak_n:
            violations.append('side_thruster_used_when_locked')
        if side_allowed and side_capacity > 0.0:
            allowed_total = side_capacity * max(0.0, side_fraction + side_margin)
            if side_total > allowed_total + side_leak_n:
                violations.append('side_thruster_fraction_exceeded')

        min_activity = float(thresholds.get('main_symmetry_min_activity_n', 5000.0))
        main_policy_thresholds = main_policy.get('thresholds', {}) or {}
        asym_abs = float(main_policy_thresholds.get('max_asymmetry_abs_n', thresholds.get('main_symmetry_abs_n', 2500.0)))
        asym_ratio = float(main_policy_thresholds.get('max_asymmetry_ratio', thresholds.get('main_symmetry_ratio', 0.15)))
        asym_limit = max(asym_abs, asym_ratio * max(main_max_abs, 1.0))
        if main_symmetry_required and main_max_abs >= min_activity and main_asymmetry > asym_limit:
            violations.append('main_propulsion_asymmetry_when_symmetry_required')
        symmetry_utilization = main_asymmetry / asym_limit if asym_limit > 1e-6 else 0.0

        if reverse_present and not reverse_allowed:
            violations.append('reverse_thrust_without_policy_permission')

        rudder_side_min_speed = float(thresholds.get('rudder_preferred_side_check_min_speed_mps', 3.0))
        rudder_side_limit = float(thresholds.get('rudder_preferred_side_force_n', 3000.0))
        rudder_side_regions = set(thresholds.get('rudder_preferred_side_check_regions', ['cruise_efficiency']) or [])
        if (
            rudder_preferred
            and operating_region in rudder_side_regions
            and self.speed_mps >= rudder_side_min_speed
            and side_total > rudder_side_limit
        ):
            violations.append('side_thruster_used_in_rudder_preferred_region')

        if not violations and not warnings and not startup_missing:
            reasons.append('actual_propulsion_matches_shadow_policy')

        status = 'COMPLIANT'
        if warnings:
            status = 'UNKNOWN'
        if startup_missing:
            status = 'INITIAL_OBSERVATION'
        if violations:
            status = 'VIOLATION'

        return {
            'schema_version': 'propulsion_policy_compliance.v1',
            'mode': 'shadow' if self.shadow_mode else 'advisory',
            'status': status,
            'violations': sorted(set(violations)),
            'warnings': sorted(set(warnings)),
            'reasons': sorted(set(reasons)),
            'speed_mps': self.speed_mps,
            'policy_region': self.policy.get('operating_region', 'UNKNOWN') if isinstance(self.policy, dict) else 'UNKNOWN',
            'policy_constraints': constraints,
            'policy_main_propulsion': main_policy,
            'actual': {
                'side_thruster_total_abs_n': side_total,
                'side_thruster_max_abs_n': side_max,
                'side_thruster_capacity_n': side_capacity,
                'main_asymmetry_n': main_asymmetry,
                'main_max_abs_n': main_max_abs,
                'main_sum_n': main_sum,
                'main_symmetry_limit_n': asym_limit,
                'main_symmetry_utilization': symmetry_utilization,
                'reverse_present': reverse_present,
            },
            'stale': stale,
            'parameter_source': self.data_policy.get('parameter_source', 'unknown'),
            'parameter_confidence': self.data_policy.get('parameter_confidence', 'unknown'),
        }

    def _metrics_array(self, report):
        actual = report['actual']
        violations = set(report.get('violations', []) or [])
        return [
            float(len(violations)),
            1.0 if 'side_thruster_used_when_locked' in violations else 0.0,
            1.0 if 'side_thruster_fraction_exceeded' in violations else 0.0,
            1.0 if 'main_propulsion_asymmetry_when_symmetry_required' in violations else 0.0,
            1.0 if 'reverse_thrust_without_policy_permission' in violations else 0.0,
            1.0 if 'side_thruster_used_in_rudder_preferred_region' in violations else 0.0,
            float(actual['side_thruster_total_abs_n']),
            float(actual['main_asymmetry_n']),
        ]

    def _tick(self):
        report = self._evaluate()
        self.compliance_pub.publish(String(data=json.dumps(report, ensure_ascii=True, sort_keys=True)))
        self.metrics_pub.publish(Float64MultiArray(data=self._metrics_array(report)))


def main(args=None):
    rclpy.init(args=args)
    node = PropulsionPolicyComplianceNode()
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
