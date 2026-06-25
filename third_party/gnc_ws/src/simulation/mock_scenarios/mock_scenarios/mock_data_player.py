import math
import random
from pathlib import Path

import rclpy
import yaml
from ament_index_python.packages import get_package_share_directory
from geometry_msgs.msg import Vector3
from nav_msgs.msg import Odometry
from sensor_msgs.msg import Imu
from ship_interfaces.msg import OceanCurrents
from std_msgs.msg import Float64, Float64MultiArray, String
from rclpy.node import Node


def _deep_get(data, dotted_key, default=None):
    value = data
    for part in dotted_key.split('.'):
        if not isinstance(value, dict) or part not in value:
            return default
        value = value[part]
    return value


def _wrap_pi(angle):
    while angle > math.pi:
        angle -= 2.0 * math.pi
    while angle < -math.pi:
        angle += 2.0 * math.pi
    return angle


def _yaw_to_quaternion(yaw):
    half = yaw * 0.5
    return 0.0, 0.0, math.sin(half), math.cos(half)


def _deg(value):
    return math.radians(float(value))


class MockDataPlayer(Node):
    """Publishes repeatable mock sensor, environment, and fault data from YAML."""

    def __init__(self):
        super().__init__('mock_data_player')
        self.declare_parameter('scenario_file', '')
        self.declare_parameter('loop', False)
        self.declare_parameter('publish_truth', True)

        self.scenario_path = self._resolve_scenario_path(
            self.get_parameter('scenario_file').value
        )
        self.scenario = self._load_scenario(self.scenario_path)
        self.loop = bool(self.get_parameter('loop').value)
        self.publish_truth = bool(self.get_parameter('publish_truth').value)
        self.rng = random.Random(int(self.scenario.get('seed', 7)))

        self.duration_s = float(self.scenario.get('duration_s', 600.0))
        self.rate_hz = float(self.scenario.get('publish_rate_hz', 10.0))
        self.dt = 1.0 / max(self.rate_hz, 1.0)
        self.start_time = self.get_clock().now()

        self.odom_pub = self.create_publisher(Odometry, '/ship/odometry', 10)
        self.truth_pub = self.create_publisher(Odometry, '/mock/truth/odometry', 10)
        self.gnss_pub = self.create_publisher(Odometry, '/mock/gnss/odometry', 10)
        self.imu_pub = self.create_publisher(Imu, '/mock/imu', 10)
        self.heading_pub = self.create_publisher(Float64, '/mock/heading', 10)
        self.wind_pub = self.create_publisher(Vector3, '/env/wind_params', 10)
        self.current_pub = self.create_publisher(OceanCurrents, '/env/ocean_currents', 10)
        self.health_pub = self.create_publisher(Float64MultiArray, '/thruster/health_status', 10)
        self.status_pub = self.create_publisher(String, '/mock/scenario_status', 10)

        self.timer = self.create_timer(self.dt, self._on_timer)
        self.get_logger().info(
            f"Loaded mock scenario {self.scenario.get('scenario_id')} from {self.scenario_path}"
        )

    def _resolve_scenario_path(self, scenario_file):
        if scenario_file:
            path = Path(str(scenario_file))
            if path.exists():
                return path
            share = Path(get_package_share_directory('mock_scenarios'))
            package_path = share / 'config' / 'scenarios' / str(scenario_file)
            if package_path.exists():
                return package_path
        share = Path(get_package_share_directory('mock_scenarios'))
        return share / 'config' / 'scenarios' / '001_straight_calm.yaml'

    def _load_scenario(self, path):
        with open(path, 'r', encoding='utf-8') as stream:
            data = yaml.safe_load(stream) or {}
        if not data.get('own_ship'):
            raise ValueError('scenario must define own_ship')
        return data

    def _elapsed(self):
        elapsed = (self.get_clock().now() - self.start_time).nanoseconds * 1e-9
        if elapsed > self.duration_s and self.loop:
            self.start_time = self.get_clock().now()
            return 0.0
        return min(elapsed, self.duration_s)

    def _track_state(self, t):
        own = self.scenario['own_ship']
        pose = own.get('initial_pose', {})
        waypoints = own.get('waypoints') or [[pose.get('x', 0.0), pose.get('y', 0.0)]]
        speed = float(own.get('nominal_speed_mps', own.get('initial_velocity', {}).get('u', 2.0)))

        x = float(waypoints[0][0])
        y = float(waypoints[0][1])
        yaw = _deg(pose.get('yaw_deg', 0.0))
        remaining = max(t * max(speed, 0.0), 0.0)
        u = speed
        v = 0.0
        r = 0.0

        for index in range(1, len(waypoints)):
            x0, y0 = float(waypoints[index - 1][0]), float(waypoints[index - 1][1])
            x1, y1 = float(waypoints[index][0]), float(waypoints[index][1])
            dx, dy = x1 - x0, y1 - y0
            length = math.hypot(dx, dy)
            if length < 1e-6:
                continue
            yaw = math.atan2(dy, dx)
            if remaining <= length:
                ratio = remaining / length
                x = x0 + ratio * dx
                y = y0 + ratio * dy
                break
            remaining -= length
            x, y = x1, y1
        else:
            u = 0.0

        return {'x': x, 'y': y, 'yaw': yaw, 'u': u, 'v': v, 'r': r}

    def _active_fault(self, key, t):
        fault = _deep_get(self.scenario, key, {}) or {}
        start = fault.get('start_s')
        duration = fault.get('duration_s', 0.0)
        if start is None:
            return False
        return float(start) <= t <= float(start) + float(duration)

    def _sensor_state(self, truth, t):
        state = dict(truth)
        gnss = _deep_get(self.scenario, 'sensors.gnss', {}) or {}
        compass = _deep_get(self.scenario, 'sensors.compass', {}) or {}
        state['x'] += self.rng.gauss(0.0, float(gnss.get('noise_std_m', 0.3)))
        state['y'] += self.rng.gauss(0.0, float(gnss.get('noise_std_m', 0.3)))
        state['yaw'] += self.rng.gauss(0.0, _deg(compass.get('noise_std_deg', 0.2)))

        if self._active_fault('sensors.gnss.jump', t):
            jump = _deep_get(self.scenario, 'sensors.gnss.jump', {})
            state['x'] += float(jump.get('dx_m', 0.0))
            state['y'] += float(jump.get('dy_m', 0.0))

        if self._active_fault('sensors.compass.bias', t):
            bias = _deep_get(self.scenario, 'sensors.compass.bias', {})
            state['yaw'] += _deg(bias.get('deg', 0.0))

        if self._active_fault('sensors.gnss.outage', t):
            state['gnss_outage'] = True

        return state

    def _make_odom(self, state, frame_id, child_frame_id):
        msg = Odometry()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = frame_id
        msg.child_frame_id = child_frame_id
        msg.pose.pose.position.x = float(state['x'])
        msg.pose.pose.position.y = float(state['y'])
        qx, qy, qz, qw = _yaw_to_quaternion(float(state['yaw']))
        msg.pose.pose.orientation.x = qx
        msg.pose.pose.orientation.y = qy
        msg.pose.pose.orientation.z = qz
        msg.pose.pose.orientation.w = qw
        msg.twist.twist.linear.x = float(state.get('u', 0.0))
        msg.twist.twist.linear.y = float(state.get('v', 0.0))
        msg.twist.twist.angular.z = float(state.get('r', 0.0))
        pos_var = float(_deep_get(self.scenario, 'sensors.gnss.noise_std_m', 1.0)) ** 2
        yaw_var = _deg(_deep_get(self.scenario, 'sensors.compass.noise_std_deg', 1.0)) ** 2
        msg.pose.covariance[0] = pos_var
        msg.pose.covariance[7] = pos_var
        msg.pose.covariance[35] = yaw_var
        return msg

    def _publish_environment(self):
        env = self.scenario.get('environment', {})
        wind = env.get('wind', {})
        current = env.get('current', {})

        wind_msg = Vector3()
        wind_msg.x = float(wind.get('speed_mps', 0.0))
        wind_msg.y = float(wind.get('direction_deg', 0.0))
        wind_msg.z = float(wind.get('anemometer_height_m', 10.0))
        self.wind_pub.publish(wind_msg)

        cur_msg = OceanCurrents()
        cur_msg.v_tide = float(current.get('tide_speed_mps', current.get('speed_mps', 0.0)))
        cur_msg.dir_tide = float(current.get('tide_direction_deg', current.get('direction_deg', 0.0)))
        cur_msg.v_wind = float(current.get('wind_drift_speed_mps', 0.0))
        cur_msg.dir_wind = float(current.get('wind_drift_direction_deg', cur_msg.dir_tide))
        cur_msg.v_circ = float(current.get('circulation_speed_mps', 0.0))
        cur_msg.dir_circ = float(current.get('circulation_direction_deg', cur_msg.dir_tide))
        self.current_pub.publish(cur_msg)

    def _publish_faults(self, t):
        health = [1.0] * int(_deep_get(self.scenario, 'thrusters.count', 7))
        for fault in _deep_get(self.scenario, 'thrusters.failures', []) or []:
            start = float(fault.get('start_s', 0.0))
            duration = float(fault.get('duration_s', self.duration_s))
            if start <= t <= start + duration:
                index = int(fault.get('index', -1))
                if 0 <= index < len(health):
                    health[index] = float(fault.get('health', 0.0))
        msg = Float64MultiArray()
        msg.data = health
        self.health_pub.publish(msg)

    def _publish_imu_heading(self, state):
        imu_cfg = _deep_get(self.scenario, 'sensors.imu', {}) or {}
        imu = Imu()
        imu.header.stamp = self.get_clock().now().to_msg()
        imu.header.frame_id = 'imu_link'
        qx, qy, qz, qw = _yaw_to_quaternion(state['yaw'])
        imu.orientation.x = qx
        imu.orientation.y = qy
        imu.orientation.z = qz
        imu.orientation.w = qw
        imu.angular_velocity.z = state.get('r', 0.0) + self.rng.gauss(
            0.0, _deg(imu_cfg.get('gyro_noise_deg_s', 0.02))
        )
        self.imu_pub.publish(imu)

        heading = Float64()
        heading.data = _wrap_pi(state['yaw'])
        self.heading_pub.publish(heading)

    def _on_timer(self):
        t = self._elapsed()
        truth = self._track_state(t)
        sensor = self._sensor_state(truth, t)

        if self.publish_truth:
            self.truth_pub.publish(self._make_odom(truth, 'map', 'base_link_truth'))

        if not sensor.get('gnss_outage', False):
            self.gnss_pub.publish(self._make_odom(sensor, 'map', 'base_link_gnss'))
            self.odom_pub.publish(self._make_odom(sensor, 'map', 'base_link'))

        self._publish_imu_heading(sensor)
        self._publish_environment()
        self._publish_faults(t)

        status = String()
        status.data = (
            f"{self.scenario.get('scenario_id','unknown')} t={t:.1f}s "
            f"x={truth['x']:.1f} y={truth['y']:.1f} yaw={math.degrees(truth['yaw']):.1f}"
        )
        self.status_pub.publish(status)


def main(args=None):
    rclpy.init(args=args)
    node = MockDataPlayer()
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
