from pathlib import Path
import json
import math

import rclpy
import yaml
from geometry_msgs.msg import WrenchStamped
from rclpy.node import Node
from std_msgs.msg import Float64, Float64MultiArray, String


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


class CaptainDecisionNode(Node):
    """Advisory captain-like decision layer.

    The node converts mission, safety, environment, and actuator observations
    into an explicit decision. It is intentionally advisory in this phase; it
    does not command actuators or override /cmd_tau.
    """

    def __init__(self):
        super().__init__('captain_decision_node')
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

        self.mission_status = {}
        self.safety_status = {}
        self.navigation_status = {}
        self.environment_force_n = 0.0
        self.actuator_available = 0.0
        self.actuator_total = 0.0

        self.last_mission_time = None
        self.last_safety_time = None
        self.last_navigation_time = None
        self.last_actuator_time = None
        self.last_environment_time = None
        self.start_time = self.get_clock().now()

        self.create_subscription(String, '/mission/status', self._on_mission_status, 10)
        self.create_subscription(String, '/safety/status', self._on_safety_status, 10)
        self.create_subscription(String, '/navigation/status', self._on_navigation_status, 10)
        self.create_subscription(Float64MultiArray, '/actuator/capability', self._on_actuator_capability, 10)
        self.create_subscription(WrenchStamped, '/env/total_load', self._on_environment_load, 10)

        self.decision_pub = self.create_publisher(String, '/captain/decision', 10)
        self.recommended_speed_pub = self.create_publisher(Float64, '/captain/recommended_speed', 10)

        self.timer = self.create_timer(self.period_s, self._tick)
        self.get_logger().info(
            f'CaptainDecision shadow_mode={self.shadow_mode} config={self.config_file or "<defaults>"}'
        )

    def _on_mission_status(self, msg):
        self.mission_status = _json_or_raw(msg.data)
        self.last_mission_time = self.get_clock().now()

    def _on_safety_status(self, msg):
        self.safety_status = _json_or_raw(msg.data)
        self.last_safety_time = self.get_clock().now()

    def _on_navigation_status(self, msg):
        self.navigation_status = _json_or_raw(msg.data)
        self.last_navigation_time = self.get_clock().now()

    def _on_actuator_capability(self, msg):
        data = list(msg.data)
        if len(data) >= 2:
            self.actuator_available = float(data[0])
            self.actuator_total = float(data[1])
        self.last_actuator_time = self.get_clock().now()

    def _on_environment_load(self, msg):
        force = msg.wrench.force
        self.environment_force_n = math.hypot(float(force.x), float(force.y))
        self.last_environment_time = self.get_clock().now()

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
            for name in ('mission_status', 'safety_status', 'navigation_status', 'actuator_capability')
            if stale.get(name)
        ]
        grace_s = float(self.config.get('initial_observation_grace_s', 0.0) or 0.0)
        if missing and self._elapsed_since(self.start_time) <= grace_s:
            return missing
        return []

    def _route_risk(self):
        xte = float(self.mission_status.get('cross_track_error_m', 0.0) or 0.0)
        limit = float(self.mission_status.get('route_xte_limit_m', 0.0) or 0.0)
        ratio = xte / limit if limit > 1.0e-6 else 0.0
        cfg = self.config.get('route_corridor', {}) or {}
        return {
            'cross_track_error_m': xte,
            'route_xte_limit_m': limit,
            'route_xte_ratio': ratio,
            'caution': bool(cfg.get('enabled', True)) and limit > 0.0 and ratio >= float(cfg.get('caution_ratio', 0.7)),
            'rejoin': bool(cfg.get('enabled', True)) and limit > 0.0 and ratio >= float(cfg.get('rejoin_ratio', 1.0)),
            'hold': bool(cfg.get('enabled', True)) and limit > 0.0 and ratio >= float(cfg.get('hold_ratio', 1.15)),
            'abort': bool(cfg.get('enabled', True)) and limit > 0.0 and ratio >= float(cfg.get('abort_ratio', 1.3)),
        }

    def _environment_risk(self):
        cfg = self.config.get('environment', {}) or {}
        force = self.environment_force_n
        return {
            'environment_force_n': force,
            'caution': force >= float(cfg.get('caution_force_n', float('inf'))),
            'hold': force >= float(cfg.get('hold_force_n', float('inf'))),
            'abort': force >= float(cfg.get('abort_force_n', float('inf'))),
        }

    def _actuator_risk(self):
        cfg = self.config.get('actuator', {}) or {}
        if self.actuator_total <= 0.0:
            ratio = 1.0
        else:
            ratio = self.actuator_available / self.actuator_total
        return {
            'available_ratio': ratio,
            'degraded': ratio < float(cfg.get('degraded_available_ratio', 0.85)),
        }

    def _speed_for(self, action, phase):
        actions = self.config.get('actions', {}) or {}
        phase_caps = self.config.get('phase_speed_caps_mps', {}) or {}
        action_speed = float((actions.get(action) or {}).get('recommended_speed_mps', 0.0))
        phase_cap = float(phase_caps.get(phase, action_speed))
        if action in {'continue', 'reduce_speed', 'rejoin_corridor'}:
            return min(action_speed, phase_cap)
        return action_speed

    def _decide(self):
        phase = str(self.mission_status.get('phase', 'UNKNOWN'))
        safety_state = str(self.safety_status.get('status', 'UNKNOWN')).upper()
        nav_state = str(self.navigation_status.get('status', 'UNKNOWN')).upper()
        route = self._route_risk()
        environment = self._environment_risk()
        actuator = self._actuator_risk()

        reasons = []
        stale = {
            'mission_status': self._is_stale('mission_status', self.last_mission_time),
            'safety_status': self._is_stale('safety_status', self.last_safety_time),
            'navigation_status': self._is_stale('navigation_status', self.last_navigation_time),
            'actuator_capability': self._is_stale('actuator_capability', self.last_actuator_time),
        }
        if any(stale.values()):
            startup_missing = self._startup_waiting_for_inputs(stale)
            if startup_missing:
                reasons.extend([f'{name}_initial_observation_pending' for name in startup_missing])
                action = 'wait_for_initial_observation'
            else:
                reasons.extend([f'{name}_stale' for name, is_stale in stale.items() if is_stale])
                action = 'request_human_confirmation'
        elif route['abort'] or environment['abort'] or safety_state == 'ABORT_REQUIRED':
            action = 'abort_escape_recommended'
            if route['abort']:
                reasons.append('route_xte_abort_threshold')
            if environment['abort']:
                reasons.append('environment_abort_threshold')
            if safety_state == 'ABORT_REQUIRED':
                reasons.append('safety_abort_required')
        elif route['hold'] or environment['hold'] or safety_state == 'DEGRADED' or actuator['degraded']:
            action = 'hold_or_manual_handover'
            if route['hold']:
                reasons.append('route_xte_hold_threshold')
            if environment['hold']:
                reasons.append('environment_hold_threshold')
            if safety_state == 'DEGRADED':
                reasons.append('safety_degraded')
            if actuator['degraded']:
                reasons.append('actuator_capability_degraded')
        elif route['rejoin']:
            action = 'rejoin_corridor'
            reasons.append('route_xte_rejoin_threshold')
        elif route['caution'] or environment['caution'] or nav_state not in {'NOMINAL', 'UNKNOWN'}:
            action = 'reduce_speed'
            if route['caution']:
                reasons.append('route_xte_caution_threshold')
            if environment['caution']:
                reasons.append('environment_caution_threshold')
            if nav_state not in {'NOMINAL', 'UNKNOWN'}:
                reasons.append('navigation_not_nominal')
        else:
            action = 'continue'
            reasons.append('all_observed_limits_nominal')

        return {
            'schema_version': 'captain_decision.v1',
            'mode': 'shadow' if self.shadow_mode else 'advisory',
            'action': action,
            'phase': phase,
            'reasons': reasons,
            'recommended_speed_mps': self._speed_for(action, phase),
            'route': route,
            'environment': environment,
            'actuator': actuator,
            'safety_status': safety_state,
            'navigation_status': nav_state,
            'stale': stale,
            'parameter_source': self.data_policy.get('parameter_source', 'unknown'),
            'parameter_confidence': self.data_policy.get('parameter_confidence', 'unknown'),
        }

    def _tick(self):
        decision = self._decide()
        self.decision_pub.publish(String(data=json.dumps(decision, ensure_ascii=True, sort_keys=True)))
        self.recommended_speed_pub.publish(Float64(data=float(decision['recommended_speed_mps'])))


def main(args=None):
    rclpy.init(args=args)
    node = CaptainDecisionNode()
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
