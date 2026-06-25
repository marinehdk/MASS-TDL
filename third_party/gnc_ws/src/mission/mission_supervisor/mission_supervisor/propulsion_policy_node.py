from pathlib import Path
import json
import math

import rclpy
import yaml
from geometry_msgs.msg import WrenchStamped
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


def _clamp(value, low, high):
    return max(low, min(high, value))


class MainPropulsionSymmetryPolicy:
    """Shadow policy for equalized main-propeller use.

    This is intentionally advisory-only. The allocator can consume the boolean
    contract fields, while richer policy details stay observable for reports and
    later allocator integration.
    """

    def __init__(self, config, data_policy):
        self.config = config or {}
        self.data_policy = data_policy or {}

    def evaluate(self, operating_region, action, speed_mps, cmd_tau, reasons):
        cfg = self.config
        symmetry_cfg = cfg.get('symmetry_policy', {}) or {}

        enabled = _as_bool(symmetry_cfg.get('enabled', cfg.get('require_symmetry_in_cruise', True)))
        mode = str(symmetry_cfg.get('mode', 'shadow'))
        enforcement = str(symmetry_cfg.get('enforcement', 'advisory_only'))
        required_regions = set(symmetry_cfg.get('required_regions', ['cruise_efficiency']) or [])
        release_regions = set(
            symmetry_cfg.get(
                'release_regions',
                ['turn_or_rejoin', 'low_speed_maneuvering', 'degraded_hold', 'emergency_abort', 'initial_observation'],
            ) or []
        )
        allow_actions = set(cfg.get('allow_differential_actions', []) or [])
        min_speed = float(symmetry_cfg.get('min_speed_mps', cfg.get('symmetry_min_speed_mps', 3.0)))
        yaw_deadband = float(symmetry_cfg.get('yaw_deadband_nm', cfg.get('symmetry_yaw_deadband_nm', 0.0)))
        lateral_deadband = float(
            symmetry_cfg.get('lateral_deadband_n', cfg.get('symmetry_lateral_deadband_n', 0.0))
        )
        asym_abs = float(symmetry_cfg.get('max_asymmetry_abs_n', cfg.get('symmetry_max_asymmetry_abs_n', 2500.0)))
        asym_ratio = float(symmetry_cfg.get('max_asymmetry_ratio', cfg.get('symmetry_max_asymmetry_ratio', 0.15)))

        yaw_abs = abs(float(cmd_tau.get('yaw_nm', 0.0) or 0.0))
        sway_abs = abs(float(cmd_tau.get('sway_n', 0.0) or 0.0))

        blockers = []
        if not enabled:
            blockers.append('main_symmetry_policy_disabled')
        if operating_region not in required_regions:
            blockers.append('main_symmetry_region_not_required')
        if operating_region in release_regions:
            blockers.append('main_symmetry_released_for_maneuvering')
        if action in allow_actions:
            blockers.append('main_symmetry_released_by_captain_action')
        if speed_mps < min_speed:
            blockers.append('main_symmetry_below_min_speed')
        if yaw_abs > yaw_deadband:
            blockers.append('main_symmetry_released_by_yaw_demand')
        if sway_abs > lateral_deadband:
            blockers.append('main_symmetry_released_by_sway_demand')

        symmetry_required = not blockers
        if symmetry_required:
            reasons.append('main_propulsion_symmetry_policy_required')
        else:
            reasons.append('main_propulsion_symmetry_policy_released')
            reasons.extend(blockers)

        return {
            'schema_version': 'main_propulsion_symmetry_policy.v1',
            'mode': mode,
            'enforcement': enforcement,
            'intent': 'symmetric_main_thrust' if symmetry_required else 'differential_main_thrust_available',
            'symmetry_required': bool(symmetry_required),
            'differential_allowed': bool(not symmetry_required),
            'operating_region': operating_region,
            'speed_mps': float(speed_mps),
            'yaw_demand_nm': float(cmd_tau.get('yaw_nm', 0.0) or 0.0),
            'sway_demand_n': float(cmd_tau.get('sway_n', 0.0) or 0.0),
            'thresholds': {
                'required_regions': sorted(required_regions),
                'release_regions': sorted(release_regions),
                'min_speed_mps': min_speed,
                'yaw_deadband_nm': yaw_deadband,
                'lateral_deadband_n': lateral_deadband,
                'max_asymmetry_abs_n': asym_abs,
                'max_asymmetry_ratio': asym_ratio,
            },
            'release_reasons': blockers,
            'parameter_source': self.data_policy.get('parameter_source', 'unknown'),
            'parameter_confidence': self.data_policy.get('parameter_confidence', 'unknown'),
        }


class PropulsionPolicyNode(Node):
    """Shadow-mode propulsion strategy layer.

    This node translates captain intent, mission phase, vessel speed, requested
    body force, environment load, and actuator capability into propulsion usage
    constraints. It does not override the allocator in this phase.
    """

    def __init__(self):
        super().__init__('propulsion_policy_node')
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
        self.main_symmetry_policy = MainPropulsionSymmetryPolicy(
            self.config.get('main_propulsion', {}) or {},
            self.data_policy,
        )

        rate = float(self.get_parameter('publish_rate_hz').value or self.config.get('publish_rate_hz', 2.0))
        self.period_s = 1.0 / max(rate, 0.1)

        self.mission_status = {}
        self.captain_decision = {}
        self.speed_mps = 0.0
        self.cmd_tau = {'surge_n': 0.0, 'sway_n': 0.0, 'yaw_nm': 0.0}
        self.environment_force_n = 0.0
        self.actuator_available = 0.0
        self.actuator_total = 0.0
        self.actuator_fault = False

        now = self.get_clock().now()
        self.start_time = now
        self.last_mission_time = None
        self.last_captain_time = None
        self.last_odom_time = None
        self.last_tau_time = None
        self.last_actuator_time = None
        self.last_environment_time = None

        self.create_subscription(String, '/mission/status', self._on_mission_status, 10)
        self.create_subscription(String, '/captain/decision', self._on_captain_decision, 10)
        self.create_subscription(Odometry, '/ship/odometry', self._on_odometry, 10)
        self.create_subscription(WrenchStamped, '/cmd_tau', self._on_cmd_tau, 10)
        self.create_subscription(Float64MultiArray, '/actuator/capability', self._on_actuator_capability, 10)
        self.create_subscription(WrenchStamped, '/env/total_load', self._on_environment_load, 10)

        self.policy_pub = self.create_publisher(String, '/propulsion/policy', 10)
        self.constraints_pub = self.create_publisher(Float64MultiArray, '/propulsion/constraints', 10)

        self.timer = self.create_timer(self.period_s, self._tick)
        self.get_logger().info(
            f'PropulsionPolicy shadow_mode={self.shadow_mode} config={self.config_file or "<defaults>"}'
        )

    def _on_mission_status(self, msg):
        self.mission_status = _json_or_raw(msg.data)
        self.last_mission_time = self.get_clock().now()

    def _on_captain_decision(self, msg):
        self.captain_decision = _json_or_raw(msg.data)
        self.last_captain_time = self.get_clock().now()

    def _on_odometry(self, msg):
        linear = msg.twist.twist.linear
        self.speed_mps = math.hypot(float(linear.x), float(linear.y))
        self.last_odom_time = self.get_clock().now()

    def _on_cmd_tau(self, msg):
        self.cmd_tau = {
            'surge_n': float(msg.wrench.force.x),
            'sway_n': float(msg.wrench.force.y),
            'yaw_nm': float(msg.wrench.torque.z),
        }
        self.last_tau_time = self.get_clock().now()

    def _on_actuator_capability(self, msg):
        data = list(msg.data)
        if len(data) >= 2:
            self.actuator_available = float(data[0])
            self.actuator_total = float(data[1])
        if len(data) >= 3:
            self.actuator_fault = float(data[2]) > 0.5
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
            for name in ('mission_status', 'captain_decision', 'odometry', 'cmd_tau', 'actuator_capability')
            if stale.get(name)
        ]
        grace_s = float(self.config.get('initial_observation_grace_s', 0.0) or 0.0)
        if missing and self._elapsed_since(self.start_time) <= grace_s:
            return missing
        return []

    def _phase(self):
        return str(self.mission_status.get('phase', 'UNKNOWN')).upper()

    def _captain_action(self):
        return str(self.captain_decision.get('action', 'UNKNOWN'))

    def _actuator_ratio(self):
        if self.actuator_total <= 0.0:
            return 1.0
        return _clamp(self.actuator_available / self.actuator_total, 0.0, 1.0)

    def _environment_level(self):
        cfg = self.config.get('environment', {}) or {}
        force = self.environment_force_n
        if force >= float(cfg.get('hold_force_n', float('inf'))):
            return 'hold'
        if force >= float(cfg.get('caution_force_n', float('inf'))):
            return 'caution'
        return 'nominal'

    def _operating_region(self, phase, action, reasons):
        speeds = self.config.get('speed_regions_mps', {}) or {}
        low_speed_max = float(speeds.get('low_speed_max', 1.5))
        maneuvering_max = float(speeds.get('maneuvering_max', 3.0))
        cruise_min = float(speeds.get('cruise_min', 5.0))
        turn_cfg = self.config.get('turn_detection', {}) or {}
        turn_yaw_nm = float(turn_cfg.get('yaw_moment_nm', 120000.0))
        turn_sway_n = float(turn_cfg.get('sway_force_n', 20000.0))

        if action == 'abort_escape_recommended':
            reasons.append('captain_abort_action')
            return 'emergency_abort'
        if action in {'hold_or_manual_handover', 'request_human_confirmation'}:
            reasons.append('captain_hold_or_confirmation_action')
            return 'degraded_hold'
        if phase in {'STANDBY', 'BERTH_OR_WORK', 'COMPLETE'} or self.speed_mps <= low_speed_max:
            reasons.append('low_speed_or_station_keeping_phase')
            return 'low_speed_maneuvering'
        if phase in {'DECEL', 'REPORT', 'APPROACH'} or action in {'reduce_speed', 'rejoin_corridor'}:
            reasons.append('turn_decel_or_rejoin_phase')
            return 'turn_or_rejoin'
        if (
            abs(float(self.cmd_tau.get('yaw_nm', 0.0) or 0.0)) >= turn_yaw_nm
            or abs(float(self.cmd_tau.get('sway_n', 0.0) or 0.0)) >= turn_sway_n
        ):
            reasons.append('large_control_demand_turn_or_rejoin')
            return 'turn_or_rejoin'
        if self.speed_mps >= cruise_min or phase == 'CRUISE':
            reasons.append('cruise_efficiency_region')
            return 'cruise_efficiency'
        if self.speed_mps <= maneuvering_max:
            reasons.append('maneuvering_speed_region')
            return 'low_speed_maneuvering'
        reasons.append('default_transit_region')
        return 'turn_or_rejoin'

    def _side_thruster_fraction(self, phase, action, reasons):
        cfg = self.config.get('side_thruster', {}) or {}
        derate_start = float(cfg.get('derate_start_speed_mps', 2.0))
        lockout = float(cfg.get('lockout_speed_mps', 4.0))
        low_fraction = float(cfg.get('max_fraction_low_speed', 1.0))
        transit_fraction = float(cfg.get('max_fraction_transit', 0.0))
        emergency_actions = set(cfg.get('emergency_actions', []) or [])
        allowed_phases = set(cfg.get('allowed_phases', []) or [])
        emergency_max = float(cfg.get('emergency_unlock_max_speed_mps', 3.5))

        if action in emergency_actions and self.speed_mps <= emergency_max:
            reasons.append('side_thruster_emergency_unlock')
            return True, low_fraction
        if phase in allowed_phases and self.speed_mps < lockout:
            reasons.append('side_thruster_allowed_phase')
            if self.speed_mps <= derate_start:
                return True, low_fraction
        if self.speed_mps >= lockout:
            reasons.append('side_thruster_high_speed_lockout')
            return False, transit_fraction
        if self.speed_mps <= derate_start:
            reasons.append('side_thruster_low_speed_available')
            return True, low_fraction

        span = max(lockout - derate_start, 1e-6)
        ratio = (self.speed_mps - derate_start) / span
        fraction = low_fraction + (transit_fraction - low_fraction) * ratio
        allowed = fraction > 1e-3
        reasons.append('side_thruster_derated_by_speed')
        return allowed, _clamp(fraction, transit_fraction, low_fraction)

    def _main_propulsion_constraints(self, operating_region, action, reasons):
        policy = self.main_symmetry_policy.evaluate(
            operating_region,
            action,
            self.speed_mps,
            self.cmd_tau,
            reasons,
        )
        return policy, bool(policy['symmetry_required']), bool(policy['differential_allowed'])

    def _rudder_preferred(self, phase, reasons):
        cfg = self.config.get('rudder', {}) or {}
        min_speed = float(cfg.get('effective_min_speed_mps', 1.0))
        preferred_speed = float(cfg.get('preferred_above_speed_mps', 3.0))
        preferred_phases = set(cfg.get('preferred_phases', []) or [])
        if self.speed_mps < min_speed:
            reasons.append('rudder_below_effective_speed')
            return False
        if phase in preferred_phases or self.speed_mps >= preferred_speed:
            reasons.append('rudder_preferred_for_transit_control')
            return True
        reasons.append('rudder_available_not_preferred')
        return False

    def _reverse_allowed(self, phase, action, reasons):
        cfg = self.config.get('reverse', {}) or {}
        max_speed = float(cfg.get('allowed_below_speed_mps', 0.8))
        braking_max_speed = float(cfg.get('controlled_braking_max_speed_mps', max_speed))
        braking_surge_request = float(cfg.get('controlled_braking_surge_request_n', -1000.0))
        braking_phases = set(cfg.get('controlled_braking_phases', []) or [])
        phases = set(cfg.get('allowed_phases', []) or [])
        actions = set(cfg.get('allowed_actions', []) or [])
        low_speed_explicit = self.speed_mps <= max_speed and (phase in phases or action in actions)
        controlled_braking = (
            phase in braking_phases
            and self.speed_mps <= braking_max_speed
            and float(self.cmd_tau.get('surge_n', 0.0) or 0.0) <= braking_surge_request
        )
        allowed = low_speed_explicit or controlled_braking
        if allowed:
            if controlled_braking:
                reasons.append('reverse_allowed_controlled_braking')
            else:
                reasons.append('reverse_allowed_low_speed_explicit')
        else:
            reasons.append('reverse_locked_without_low_speed_intent')
        return allowed

    def _build_policy(self):
        phase = self._phase()
        action = self._captain_action()
        reasons = []
        stale = {
            'mission_status': self._is_stale('mission_status', self.last_mission_time),
            'captain_decision': self._is_stale('captain_decision', self.last_captain_time),
            'odometry': self._is_stale('odometry', self.last_odom_time),
            'cmd_tau': self._is_stale('cmd_tau', self.last_tau_time),
            'actuator_capability': self._is_stale('actuator_capability', self.last_actuator_time),
        }
        startup_missing = self._startup_waiting_for_inputs(stale)
        if startup_missing:
            reasons.extend([f'{name}_initial_observation_pending' for name in startup_missing])
            operating_region = 'initial_observation'
        elif any(stale.values()):
            reasons.extend([f'{name}_stale' for name, is_stale in stale.items() if is_stale])
            operating_region = 'degraded_hold'
        else:
            operating_region = self._operating_region(phase, action, reasons)

        environment_level = self._environment_level()
        if environment_level != 'nominal':
            reasons.append(f'environment_{environment_level}')
        if self.actuator_fault:
            reasons.append('actuator_fault_detected')

        main_policy, main_symmetry, differential_allowed = self._main_propulsion_constraints(
            operating_region,
            action,
            reasons,
        )
        rudder_preferred = self._rudder_preferred(phase, reasons)
        side_allowed, side_fraction = self._side_thruster_fraction(phase, action, reasons)
        side_cfg = self.config.get('side_thruster', {}) or {}
        side_fraction_regions = set(side_cfg.get('fraction_enforcement_regions', ['cruise_efficiency']) or [])
        side_fraction_min_speed = float(side_cfg.get('fraction_enforcement_min_speed_mps', 4.0))
        side_fraction_speed_derated = 'side_thruster_derated_by_speed' in reasons
        if (
            side_allowed
            and not side_fraction_speed_derated
            and (operating_region not in side_fraction_regions or self.speed_mps < side_fraction_min_speed)
        ):
            if side_fraction < 1.0:
                reasons.append('side_thruster_fraction_unrestricted_for_maneuvering_region')
            side_fraction = 1.0
        rudder_cap_min_speed = float(side_cfg.get('rudder_preferred_cap_min_speed_mps', 3.0))
        rudder_cap_fraction = float(side_cfg.get('rudder_preferred_max_fraction', side_fraction))
        rudder_cap_regions = set(side_cfg.get('rudder_preferred_cap_regions', ['cruise_efficiency']) or [])
        if (
            rudder_preferred
            and side_allowed
            and self.speed_mps >= rudder_cap_min_speed
            and operating_region in rudder_cap_regions
        ):
            capped_fraction = min(side_fraction, max(0.0, rudder_cap_fraction))
            if capped_fraction < side_fraction:
                reasons.append('side_thruster_capped_by_rudder_preferred_region')
                side_fraction = capped_fraction
                side_allowed = side_fraction > 1e-3
        reverse_allowed = self._reverse_allowed(phase, action, reasons)

        constraints = {
            'side_thruster_allowed': bool(side_allowed),
            'side_thruster_max_fraction': float(side_fraction),
            'main_propulsion_symmetry_required': bool(main_symmetry),
            'differential_main_thrust_allowed': bool(differential_allowed),
            'rudder_preferred': bool(rudder_preferred),
            'reverse_allowed': bool(reverse_allowed),
            'actuator_available_ratio': self._actuator_ratio(),
        }

        return {
            'schema_version': 'propulsion_policy.v1',
            'mode': 'shadow' if self.shadow_mode else 'advisory',
            'operating_region': operating_region,
            'phase': phase,
            'captain_action': action,
            'speed_mps': self.speed_mps,
            'cmd_tau': dict(self.cmd_tau),
            'environment_force_n': self.environment_force_n,
            'environment_level': environment_level,
            'main_propulsion_policy': main_policy,
            'constraints': constraints,
            'reasons': sorted(set(reasons)),
            'stale': stale,
            'parameter_source': self.data_policy.get('parameter_source', 'unknown'),
            'parameter_confidence': self.data_policy.get('parameter_confidence', 'unknown'),
        }

    def _constraints_array(self, policy):
        c = policy['constraints']
        return [
            1.0 if c['side_thruster_allowed'] else 0.0,
            float(c['side_thruster_max_fraction']),
            1.0 if c['main_propulsion_symmetry_required'] else 0.0,
            1.0 if c['differential_main_thrust_allowed'] else 0.0,
            1.0 if c['rudder_preferred'] else 0.0,
            1.0 if c['reverse_allowed'] else 0.0,
            float(c['actuator_available_ratio']),
        ]

    def _tick(self):
        policy = self._build_policy()
        self.policy_pub.publish(String(data=json.dumps(policy, ensure_ascii=True, sort_keys=True)))
        self.constraints_pub.publish(Float64MultiArray(data=self._constraints_array(policy)))


def main(args=None):
    rclpy.init(args=args)
    node = PropulsionPolicyNode()
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
