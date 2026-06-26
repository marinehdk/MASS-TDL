from pathlib import Path
import json
import math

import rclpy
import yaml
from geometry_msgs.msg import WrenchStamped
from rclpy.node import Node
from std_msgs.msg import Bool, Float64, Float64MultiArray, String


def _json_or_raw(text):
    if not text:
        return {}
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return {'raw': text}


class ValidationObserver(Node):
    """Records mission and safety observability topics for scenario acceptance."""

    def __init__(self):
        super().__init__('validation_observer')
        self.declare_parameter('log_dir', '/tmp/mass_adas_ab_validation/')
        self.declare_parameter('scenario_id', 'scenario')
        self.declare_parameter('write_rate_hz', 1.0)

        self.log_dir = Path(str(self.get_parameter('log_dir').value))
        self.scenario_id = str(self.get_parameter('scenario_id').value or 'scenario')
        rate = float(self.get_parameter('write_rate_hz').value or 1.0)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.events_path = self.log_dir / 'observability_events.jsonl'
        self.metrics_path = self.log_dir / 'observability_metrics.yaml'

        self.phases_seen = set()
        self.safety_statuses_seen = set()
        self.safety_reasons_seen = set()
        self.captain_alert_count = 0
        self.abort_request_count = 0
        self.environment_load_count = 0
        self.captain_decision_count = 0
        self.captain_actions_seen = set()
        self.captain_decision_reasons_seen = set()
        self.max_captain_recommended_speed_mps = 0.0
        self.propulsion_policy_count = 0
        self.propulsion_regions_seen = set()
        self.propulsion_policy_reasons_seen = set()
        self.side_thruster_allowed_count = 0
        self.side_thruster_lockout_count = 0
        self.main_symmetry_required_count = 0
        self.main_symmetry_policy_intents_seen = set()
        self.main_symmetry_policy_modes_seen = set()
        self.propulsion_compliance_count = 0
        self.propulsion_compliance_statuses_seen = set()
        self.propulsion_compliance_violations_seen = set()
        self.propulsion_compliance_warnings_seen = set()
        self.max_propulsion_violation_count = 0.0
        self.max_actual_side_thruster_total_abs_n = 0.0
        self.max_actual_main_asymmetry_n = 0.0
        self.max_environment_force_n = 0.0
        self.actuator_fault_detected = False
        self.navigation_fault_detected = False
        self.hard_turn_violation = False
        self.large_forward_thrust_violation = False

        self.metrics = {
            'navigation_fault_detected': False,
            'actuator_fault_detected': False,
            'should_enter_degraded_mode': False,
            'should_alert_captain': False,
            'should_publish_arrival_status': False,
            'should_enter_hold_or_manual_handover': False,
            'should_not_command_hard_turn': True,
            'should_not_keep_reissuing_large_forward_thrust': True,
            'should_publish_captain_decision': False,
            'should_publish_propulsion_policy': False,
            'should_publish_propulsion_compliance': False,
            'propulsion_policy_violation_detected': False,
        }

        self.create_subscription(String, '/mission/phase', self._on_mission_phase, 10)
        self.create_subscription(String, '/mission/status', self._on_mission_status, 10)
        self.create_subscription(String, '/mission/active_gate', self._on_active_gate, 10)
        self.create_subscription(String, '/safety/status', self._on_safety_status, 10)
        self.create_subscription(Bool, '/safety/abort_request', self._on_abort_request, 10)
        self.create_subscription(String, '/captain/alert', self._on_captain_alert, 10)
        self.create_subscription(String, '/navigation/status', self._on_navigation_status, 10)
        self.create_subscription(Float64MultiArray, '/actuator/capability', self._on_actuator_capability, 10)
        self.create_subscription(Float64MultiArray, '/safety/command_limits', self._on_command_limits, 10)
        self.create_subscription(WrenchStamped, '/env/total_load', self._on_environment_load, 10)
        self.create_subscription(String, '/captain/decision', self._on_captain_decision, 10)
        self.create_subscription(Float64, '/captain/recommended_speed', self._on_captain_recommended_speed, 10)
        self.create_subscription(String, '/propulsion/policy', self._on_propulsion_policy, 10)
        self.create_subscription(Float64MultiArray, '/propulsion/constraints', self._on_propulsion_constraints, 10)
        self.create_subscription(String, '/propulsion/compliance', self._on_propulsion_compliance, 10)
        self.create_subscription(Float64MultiArray, '/propulsion/compliance_metrics', self._on_propulsion_compliance_metrics, 10)

        self.timer = self.create_timer(1.0 / max(rate, 0.1), self._write_summary)
        self._write_event('observer_started', {'scenario_id': self.scenario_id})
        self._write_summary()
        self.get_logger().info(
            f'ValidationObserver scenario={self.scenario_id} log_dir={self.log_dir}'
        )

    def _write_event(self, kind, payload):
        event = {
            'kind': kind,
            'time_s': self.get_clock().now().nanoseconds * 1e-9,
            'payload': payload,
        }
        with self.events_path.open('a', encoding='utf-8') as stream:
            stream.write(json.dumps(event, ensure_ascii=True, sort_keys=True) + '\n')

    def _write_summary(self):
        metrics = dict(self.metrics)
        metrics.update({
            'mission_phases_seen': sorted(self.phases_seen),
            'safety_statuses_seen': sorted(self.safety_statuses_seen),
            'safety_reasons_seen': sorted(self.safety_reasons_seen),
            'captain_alert_count': self.captain_alert_count,
            'abort_request_count': self.abort_request_count,
            'captain_decision_count': self.captain_decision_count,
            'captain_actions_seen': sorted(self.captain_actions_seen),
            'captain_decision_reasons_seen': sorted(self.captain_decision_reasons_seen),
            'max_captain_recommended_speed_mps': self.max_captain_recommended_speed_mps,
            'propulsion_policy_count': self.propulsion_policy_count,
            'propulsion_regions_seen': sorted(self.propulsion_regions_seen),
            'propulsion_policy_reasons_seen': sorted(self.propulsion_policy_reasons_seen),
            'side_thruster_allowed_count': self.side_thruster_allowed_count,
            'side_thruster_lockout_count': self.side_thruster_lockout_count,
            'main_symmetry_required_count': self.main_symmetry_required_count,
            'main_symmetry_policy_intents_seen': sorted(self.main_symmetry_policy_intents_seen),
            'main_symmetry_policy_modes_seen': sorted(self.main_symmetry_policy_modes_seen),
            'propulsion_compliance_count': self.propulsion_compliance_count,
            'propulsion_compliance_statuses_seen': sorted(self.propulsion_compliance_statuses_seen),
            'propulsion_compliance_violations_seen': sorted(self.propulsion_compliance_violations_seen),
            'propulsion_compliance_warnings_seen': sorted(self.propulsion_compliance_warnings_seen),
            'max_propulsion_violation_count': self.max_propulsion_violation_count,
            'max_actual_side_thruster_total_abs_n': self.max_actual_side_thruster_total_abs_n,
            'max_actual_main_asymmetry_n': self.max_actual_main_asymmetry_n,
            'environment_load_count': self.environment_load_count,
            'max_environment_force_n': self.max_environment_force_n,
        })
        report = {'scenarios': {self.scenario_id: metrics}}
        tmp_path = self.metrics_path.with_suffix(self.metrics_path.suffix + '.tmp')
        with tmp_path.open('w', encoding='utf-8') as stream:
            yaml.safe_dump(report, stream, sort_keys=True)
        tmp_path.replace(self.metrics_path)

    def _on_mission_phase(self, msg):
        phase = msg.data.strip()
        if not phase:
            return
        self.phases_seen.add(phase)
        if phase in {'STANDBY', 'BERTH_OR_WORK', 'COMPLETE'}:
            self.metrics['should_publish_arrival_status'] = True
            self.metrics['should_enter_hold_or_manual_handover'] = True
        if phase == 'ABORT_ESCAPE':
            self.metrics['should_enter_degraded_mode'] = True
            self.metrics['should_enter_hold_or_manual_handover'] = True
        self._write_event('mission_phase', {'phase': phase})

    def _on_mission_status(self, msg):
        payload = _json_or_raw(msg.data)
        gate = payload.get('active_gate', {}) if isinstance(payload, dict) else {}
        if gate.get('name') in {'standby_gate', 'complete_gate'} and gate.get('passed'):
            self.metrics['should_publish_arrival_status'] = True
        self._write_event('mission_status', payload)

    def _on_active_gate(self, msg):
        self._write_event('mission_active_gate', _json_or_raw(msg.data))

    def _on_safety_status(self, msg):
        payload = _json_or_raw(msg.data)
        status = payload.get('status')
        if status:
            self.safety_statuses_seen.add(status)
        reasons = payload.get('reasons', []) or []
        for reason in reasons:
            self.safety_reasons_seen.add(str(reason))
        if status in {'DEGRADED', 'ABORT_REQUIRED'}:
            self.metrics['should_enter_degraded_mode'] = True
        if payload.get('odometry_stale'):
            self.navigation_fault_detected = True
            self.metrics['navigation_fault_detected'] = True
        if 'actuator_fault_detected' in reasons:
            self.actuator_fault_detected = True
            self.metrics['actuator_fault_detected'] = True
        if 'cmd_tau_n_over_limit' in reasons or 'yaw_rate_over_limit' in reasons:
            self.hard_turn_violation = True
            self.metrics['should_not_command_hard_turn'] = False
        if 'cmd_tau_x_over_limit' in reasons:
            self.large_forward_thrust_violation = True
            self.metrics['should_not_keep_reissuing_large_forward_thrust'] = False
        self._write_event('safety_status', payload)

    def _on_abort_request(self, msg):
        if msg.data:
            self.abort_request_count += 1
            self.metrics['should_enter_degraded_mode'] = True
        self._write_event('safety_abort_request', {'data': bool(msg.data)})

    def _on_captain_alert(self, msg):
        if msg.data:
            self.captain_alert_count += 1
            self.metrics['should_alert_captain'] = True
        self._write_event('captain_alert', _json_or_raw(msg.data))

    def _on_navigation_status(self, msg):
        payload = _json_or_raw(msg.data)
        status = str(payload.get('status', '')).upper()
        if status and status != 'NOMINAL':
            self.navigation_fault_detected = True
            self.metrics['navigation_fault_detected'] = True
        self._write_event('navigation_status', payload)

    def _on_actuator_capability(self, msg):
        data = list(msg.data)
        if len(data) >= 3 and data[2] > 0.5:
            self.actuator_fault_detected = True
            self.metrics['actuator_fault_detected'] = True
            self.metrics['should_enter_degraded_mode'] = True
        self._write_event('actuator_capability', {'data': data})

    def _on_command_limits(self, msg):
        self._write_event('safety_command_limits', {'data': list(msg.data)})

    def _on_environment_load(self, msg):
        force = msg.wrench.force
        magnitude = math.hypot(force.x, force.y)
        self.environment_load_count += 1
        self.max_environment_force_n = max(self.max_environment_force_n, magnitude)
        self.metrics['should_report_environment_load'] = True
        self.metrics['should_publish_environment_load'] = True
        self._write_event('environment_load', {
            'force_x_n': force.x,
            'force_y_n': force.y,
            'torque_z_nm': msg.wrench.torque.z,
            'magnitude_n': magnitude,
        })

    def _on_captain_decision(self, msg):
        payload = _json_or_raw(msg.data)
        self.captain_decision_count += 1
        self.metrics['should_publish_captain_decision'] = True
        action = payload.get('action')
        if action:
            self.captain_actions_seen.add(str(action))
        for reason in payload.get('reasons', []) or []:
            self.captain_decision_reasons_seen.add(str(reason))
        self._write_event('captain_decision', payload)

    def _on_captain_recommended_speed(self, msg):
        self.max_captain_recommended_speed_mps = max(
            self.max_captain_recommended_speed_mps,
            float(msg.data),
        )
        self._write_event('captain_recommended_speed', {'data': float(msg.data)})

    def _on_propulsion_policy(self, msg):
        payload = _json_or_raw(msg.data)
        self.propulsion_policy_count += 1
        self.metrics['should_publish_propulsion_policy'] = True
        region = payload.get('operating_region')
        if region:
            self.propulsion_regions_seen.add(str(region))
        for reason in payload.get('reasons', []) or []:
            self.propulsion_policy_reasons_seen.add(str(reason))
        constraints = payload.get('constraints', {}) or {}
        main_policy = payload.get('main_propulsion_policy', {}) or {}
        if isinstance(main_policy, dict):
            intent = main_policy.get('intent')
            mode = main_policy.get('mode')
            if intent:
                self.main_symmetry_policy_intents_seen.add(str(intent))
            if mode:
                self.main_symmetry_policy_modes_seen.add(str(mode))
        if constraints.get('side_thruster_allowed'):
            self.side_thruster_allowed_count += 1
        else:
            self.side_thruster_lockout_count += 1
        if constraints.get('main_propulsion_symmetry_required'):
            self.main_symmetry_required_count += 1
        self._write_event('propulsion_policy', payload)

    def _on_propulsion_constraints(self, msg):
        self._write_event('propulsion_constraints', {'data': list(msg.data)})

    def _on_propulsion_compliance(self, msg):
        payload = _json_or_raw(msg.data)
        self.propulsion_compliance_count += 1
        self.metrics['should_publish_propulsion_compliance'] = True
        status = payload.get('status')
        if status:
            self.propulsion_compliance_statuses_seen.add(str(status))
        violations = payload.get('violations', []) or []
        if violations:
            self.metrics['propulsion_policy_violation_detected'] = True
        for violation in violations:
            self.propulsion_compliance_violations_seen.add(str(violation))
        for warning in payload.get('warnings', []) or []:
            self.propulsion_compliance_warnings_seen.add(str(warning))
        actual = payload.get('actual', {}) or {}
        self.max_actual_side_thruster_total_abs_n = max(
            self.max_actual_side_thruster_total_abs_n,
            float(actual.get('side_thruster_total_abs_n', 0.0) or 0.0),
        )
        self.max_actual_main_asymmetry_n = max(
            self.max_actual_main_asymmetry_n,
            float(actual.get('main_asymmetry_n', 0.0) or 0.0),
        )
        self._write_event('propulsion_compliance', payload)

    def _on_propulsion_compliance_metrics(self, msg):
        data = list(msg.data)
        if data:
            self.max_propulsion_violation_count = max(
                self.max_propulsion_violation_count,
                float(data[0]),
            )
        self._write_event('propulsion_compliance_metrics', {'data': data})


def main(args=None):
    rclpy.init(args=args)
    node = ValidationObserver()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            node._write_summary()
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
