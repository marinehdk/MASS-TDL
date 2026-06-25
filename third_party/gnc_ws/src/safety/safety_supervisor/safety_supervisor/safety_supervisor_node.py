import json
import math
from pathlib import Path

import rclpy
from geometry_msgs.msg import WrenchStamped
from nav_msgs.msg import Odometry
from rclpy.node import Node
from std_msgs.msg import Bool, Float64MultiArray, String
import yaml


STATUS_LEVELS = {
    'NOMINAL': 0,
    'CAUTION': 1,
    'DEGRADED': 2,
    'ABORT_REQUIRED': 3,
}


def _load_yaml(path):
    if not path:
        return {}
    p = Path(path)
    if not p.exists():
        return {}
    with p.open('r', encoding='utf-8') as stream:
        return yaml.safe_load(stream) or {}


def _status_max(left, right):
    return left if STATUS_LEVELS[left] >= STATUS_LEVELS[right] else right


class SafetySupervisorNode(Node):
    """Shadow-mode safety supervisor.

    This node is intentionally observational first. It publishes safety state and
    proposed limits, but does not alter /cmd_tau or actuator commands.
    """

    def __init__(self):
        super().__init__('safety_supervisor_node')
        self.declare_parameter('config_file', '')
        self.declare_parameter('shadow_mode', True)
        self.declare_parameter('publish_rate_hz', 5.0)
        self.declare_parameter('phase', 'default')

        config_file = self.get_parameter('config_file').value
        self.config = _load_yaml(config_file)
        self.shadow_mode = bool(self.get_parameter('shadow_mode').value)
        self.phase = str(self.get_parameter('phase').value or 'default')
        self.data_policy = self.config.get('data_policy', {}) or {}

        rate = float(self.get_parameter('publish_rate_hz').value)
        self.period_s = 1.0 / max(rate, 0.1)

        self.odom = None
        self.cmd_tau = None
        self.env_load = None
        self.health = []
        self.mission_status = {}
        self.last_odom_time = None
        self.last_env_time = None
        self.last_health_time = None
        self.last_mission_time = None
        self.last_alert = ''
        self.start_time = self.get_clock().now()

        self.create_subscription(Odometry, '/ship/odometry', self._on_odom, 10)
        self.create_subscription(WrenchStamped, '/cmd_tau', self._on_cmd_tau, 10)
        self.create_subscription(WrenchStamped, '/env/total_load', self._on_env_load, 10)
        self.create_subscription(Float64MultiArray, '/thruster/health_status', self._on_health, 10)
        self.create_subscription(String, '/mission/status', self._on_mission_status, 10)

        self.status_pub = self.create_publisher(String, '/safety/status', 10)
        self.abort_pub = self.create_publisher(Bool, '/safety/abort_request', 10)
        self.alert_pub = self.create_publisher(String, '/captain/alert', 10)
        self.limit_pub = self.create_publisher(Float64MultiArray, '/safety/command_limits', 10)
        self.nav_status_pub = self.create_publisher(String, '/navigation/status', 10)
        self.actuator_capability_pub = self.create_publisher(Float64MultiArray, '/actuator/capability', 10)

        self.timer = self.create_timer(self.period_s, self._tick)
        self.get_logger().info(
            f'SafetySupervisor shadow_mode={self.shadow_mode} config={config_file or "<defaults>"}'
        )

    def _on_odom(self, msg):
        self.odom = msg
        self.last_odom_time = self.get_clock().now()

    def _on_cmd_tau(self, msg):
        self.cmd_tau = msg

    def _on_env_load(self, msg):
        self.env_load = msg
        self.last_env_time = self.get_clock().now()

    def _on_health(self, msg):
        self.health = list(msg.data)
        self.last_health_time = self.get_clock().now()

    def _on_mission_status(self, msg):
        try:
            self.mission_status = json.loads(msg.data) if msg.data else {}
        except json.JSONDecodeError:
            self.mission_status = {}
        self.last_mission_time = self.get_clock().now()

    def _limits(self):
        phases = self.config.get('phases', {}) or {}
        return phases.get(self.phase) or phases.get('default') or {}

    def _stale_timeout(self, key, default):
        stale = self.config.get('stale_timeout_s', {}) or {}
        return float(stale.get(key, default))

    def _is_stale(self, stamp, timeout_s):
        if stamp is None:
            grace_s = float(self.config.get('startup_grace_s', 0.0))
            elapsed_s = (self.get_clock().now() - self.start_time).nanoseconds * 1e-9
            return elapsed_s > grace_s
        return (self.get_clock().now() - stamp).nanoseconds * 1e-9 > timeout_s

    def _speed_and_yaw_rate(self):
        if self.odom is None:
            return 0.0, 0.0
        twist = self.odom.twist.twist
        speed = math.hypot(twist.linear.x, twist.linear.y)
        yaw_rate_deg_s = abs(math.degrees(twist.angular.z))
        return speed, yaw_rate_deg_s

    def _environment_force(self):
        if self.env_load is None:
            return 0.0
        force = self.env_load.wrench.force
        return math.hypot(force.x, force.y)

    def _command_loads(self):
        if self.cmd_tau is None:
            return 0.0, 0.0, 0.0
        wrench = self.cmd_tau.wrench
        return abs(wrench.force.x), abs(wrench.force.y), abs(wrench.torque.z)

    def _health_counts(self):
        if not self.health:
            return 0, 0, False
        total = len(self.health)
        healthy = sum(1 for value in self.health if value > 0.5)
        return healthy, total, healthy < total

    def _evaluate(self):
        limits = self._limits()
        reasons = []
        status = 'NOMINAL'

        odom_stale = self._is_stale(self.last_odom_time, self._stale_timeout('odometry', 2.0))
        env_stale = self._is_stale(self.last_env_time, self._stale_timeout('environment_load', 5.0))
        health_stale = self._is_stale(self.last_health_time, self._stale_timeout('actuator_health', 5.0))
        mission_stale = self._is_stale(self.last_mission_time, self._stale_timeout('mission_status', 5.0))

        if odom_stale:
            status = _status_max(status, 'ABORT_REQUIRED')
            reasons.append('odometry_stale')

        speed, yaw_rate = self._speed_and_yaw_rate()
        if speed > float(limits.get('max_speed_mps', float('inf'))):
            status = _status_max(status, 'ABORT_REQUIRED')
            reasons.append('speed_over_limit')
        elif speed > float(limits.get('caution_speed_mps', float('inf'))):
            status = _status_max(status, 'CAUTION')
            reasons.append('speed_caution')

        if yaw_rate > float(limits.get('max_yaw_rate_deg_s', float('inf'))):
            status = _status_max(status, 'ABORT_REQUIRED')
            reasons.append('yaw_rate_over_limit')
        elif yaw_rate > float(limits.get('caution_yaw_rate_deg_s', float('inf'))):
            status = _status_max(status, 'CAUTION')
            reasons.append('yaw_rate_caution')

        cmd_x, cmd_y, cmd_n = self._command_loads()
        if cmd_x > float(limits.get('max_force_x_n', float('inf'))):
            status = _status_max(status, 'ABORT_REQUIRED')
            reasons.append('cmd_tau_x_over_limit')
        if cmd_y > float(limits.get('max_force_y_n', float('inf'))):
            status = _status_max(status, 'ABORT_REQUIRED')
            reasons.append('cmd_tau_y_over_limit')
        if cmd_n > float(limits.get('max_torque_z_nm', float('inf'))):
            status = _status_max(status, 'ABORT_REQUIRED')
            reasons.append('cmd_tau_n_over_limit')

        env_force = self._environment_force()
        if not env_stale:
            if env_force > float(limits.get('max_environment_force_n', float('inf'))):
                status = _status_max(status, 'ABORT_REQUIRED')
                reasons.append('environment_force_over_limit')
            elif env_force > float(limits.get('caution_environment_force_n', float('inf'))):
                status = _status_max(status, 'CAUTION')
                reasons.append('environment_force_caution')

        healthy, total, actuator_fault = self._health_counts()
        if actuator_fault:
            status = _status_max(status, 'DEGRADED')
            reasons.append('actuator_fault_detected')
        elif health_stale and total == 0:
            status = _status_max(status, 'CAUTION')
            reasons.append('actuator_health_unknown')

        route_corridor = limits.get('route_corridor', {}) or {}
        route_xte = float(self.mission_status.get('cross_track_error_m', 0.0) or 0.0)
        route_xte_limit = float(self.mission_status.get('route_xte_limit_m', 0.0) or 0.0)
        if route_corridor.get('enabled', False) and not mission_stale:
            if route_corridor.get('use_mission_limit', False) and route_xte_limit > 0.0:
                caution_xte = route_xte_limit * float(route_corridor.get('caution_ratio', 0.7))
                degraded_xte = route_xte_limit * float(route_corridor.get('degraded_ratio', 1.0))
                abort_xte = route_xte_limit * float(route_corridor.get('abort_ratio', 1.3))
            else:
                caution_xte = float(route_corridor.get('caution_xte_m', float('inf')))
                degraded_xte = float(route_corridor.get('degraded_xte_m', float('inf')))
                abort_xte = float(route_corridor.get('abort_xte_m', float('inf')))
            if route_xte > abort_xte:
                status = _status_max(status, 'ABORT_REQUIRED')
                reasons.append('route_xte_abort')
            elif route_xte > degraded_xte:
                status = _status_max(status, 'DEGRADED')
                reasons.append('route_xte_degraded')
            elif route_xte > caution_xte:
                status = _status_max(status, 'CAUTION')
                reasons.append('route_xte_caution')

        return {
            'status': status,
            'phase': self.phase,
            'shadow_mode': self.shadow_mode,
            'parameter_source': self.data_policy.get('parameter_source', 'unknown'),
            'parameter_confidence': self.data_policy.get('parameter_confidence', 'unknown'),
            'reasons': reasons,
            'speed_mps': speed,
            'yaw_rate_deg_s': yaw_rate,
            'cmd_force_x_n': cmd_x,
            'cmd_force_y_n': cmd_y,
            'cmd_torque_z_nm': cmd_n,
            'environment_force_n': env_force,
            'odometry_stale': odom_stale,
            'environment_stale': env_stale,
            'actuator_health_stale': health_stale,
            'mission_status_stale': mission_stale,
            'route_cross_track_error_m': route_xte,
            'route_cross_track_limit_m': route_xte_limit,
            'healthy_actuators': healthy,
            'total_actuators': total,
        }

    def _publish_limits(self, limits):
        msg = Float64MultiArray()
        msg.data = [
            float(limits.get('max_speed_mps', 0.0)),
            float(limits.get('max_force_x_n', 0.0)),
            float(limits.get('max_force_y_n', 0.0)),
            float(limits.get('max_torque_z_nm', 0.0)),
            float(limits.get('max_yaw_rate_deg_s', 0.0)),
            1.0 if self.shadow_mode else 0.0,
        ]
        self.limit_pub.publish(msg)

    def _tick(self):
        limits = self._limits()
        report = self._evaluate()

        text = json.dumps(report, ensure_ascii=True, sort_keys=True)
        self.status_pub.publish(String(data=text))
        self.nav_status_pub.publish(String(data=json.dumps({
            'status': 'STALE' if report['odometry_stale'] else 'NOMINAL',
            'odometry_stale': report['odometry_stale'],
        }, ensure_ascii=True, sort_keys=True)))

        healthy = float(report['healthy_actuators'])
        total = float(report['total_actuators'])
        self.actuator_capability_pub.publish(Float64MultiArray(data=[
            healthy,
            total,
            1.0 if healthy < total and total > 0.0 else 0.0,
        ]))

        # In shadow mode this node is advisory only: it must not drive the
        # mission state machine into ABORT_ESCAPE.
        abort_required = report['status'] == 'ABORT_REQUIRED' and not self.shadow_mode
        self.abort_pub.publish(Bool(data=abort_required))

        alert = ''
        if report['status'] != 'NOMINAL':
            alert = json.dumps({
                'status': report['status'],
                'reasons': report['reasons'],
                'shadow_mode': self.shadow_mode,
            }, ensure_ascii=True, sort_keys=True)
        self.alert_pub.publish(String(data=alert))
        self._publish_limits(limits)


def main(args=None):
    rclpy.init(args=args)
    node = SafetySupervisorNode()
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
