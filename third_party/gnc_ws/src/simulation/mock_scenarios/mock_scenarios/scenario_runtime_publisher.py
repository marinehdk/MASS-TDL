import math
from pathlib import Path

import rclpy
import yaml
from ament_index_python.packages import get_package_share_directory
from geometry_msgs.msg import PoseStamped, Vector3
from nav_msgs.msg import Path as NavPath
from rclpy.node import Node
from ship_interfaces.msg import OceanCurrents
from std_msgs.msg import Float64, Int32, String


def _load_yaml(path):
    with open(path, 'r', encoding='utf-8') as stream:
        return yaml.safe_load(stream) or {}


def _resolve_scenario_path(scenario_file):
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


def _yaw_to_quaternion(yaw_rad):
    half = yaw_rad * 0.5
    return 0.0, 0.0, math.sin(half), math.cos(half)


class ScenarioRuntimePublisher(Node):
    """Publishes scenario runtime inputs without replacing ship_dynamics odometry."""

    def __init__(self):
        super().__init__('scenario_runtime_publisher')
        self.declare_parameter('scenario_file', '')
        self.declare_parameter('publish_rate_hz', 1.0)
        self.declare_parameter('publish_waypoints', False)
        self.declare_parameter('publish_environment', True)
        self.declare_parameter('inject_faults', True)

        self.scenario_path = _resolve_scenario_path(
            self.get_parameter('scenario_file').value
        )
        self.scenario = _load_yaml(self.scenario_path)
        self.rate_hz = float(self.get_parameter('publish_rate_hz').value)
        self.publish_waypoints = bool(self.get_parameter('publish_waypoints').value)
        self.publish_environment = bool(self.get_parameter('publish_environment').value)
        self.inject_faults = bool(self.get_parameter('inject_faults').value)
        self.start_time = self.get_clock().now()
        self.injected_faults = set()
        self.recovered_faults = set()

        self.path_pub = self.create_publisher(NavPath, '/ship/waypoints', 10)
        self.wind_pub = self.create_publisher(Vector3, '/env/wind_params', 10)
        self.wave_pub = self.create_publisher(Vector3, '/env/wave_params', 10)
        self.current_pub = self.create_publisher(OceanCurrents, '/env/ocean_currents', 10)
        self.depth_pub = self.create_publisher(Float64, '/env/water_depth', 10)
        self.fault_pub = self.create_publisher(Int32, '/inject_fault', 10)
        self.status_pub = self.create_publisher(String, '/mock/scenario_status', 10)

        period = 1.0 / max(self.rate_hz, 0.1)
        self.timer = self.create_timer(period, self._on_timer)
        self.get_logger().info(
            f"Loaded runtime scenario {self.scenario.get('scenario_id')} from {self.scenario_path}"
        )

    def _elapsed_s(self):
        return (self.get_clock().now() - self.start_time).nanoseconds * 1e-9

    def _publish_path(self):
        waypoints = self.scenario.get('own_ship', {}).get('waypoints') or []
        if not waypoints:
            return
        msg = NavPath()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = 'odom'
        for item in waypoints:
            pose = PoseStamped()
            pose.header = msg.header
            pose.pose.position.x = float(item[0])
            pose.pose.position.y = float(item[1])
            qx, qy, qz, qw = _yaw_to_quaternion(0.0)
            pose.pose.orientation.x = qx
            pose.pose.orientation.y = qy
            pose.pose.orientation.z = qz
            pose.pose.orientation.w = qw
            msg.poses.append(pose)
        self.path_pub.publish(msg)

    def _publish_environment(self):
        env = self.scenario.get('environment', {})
        wind = env.get('wind', {}) or {}
        current = env.get('current', {}) or {}
        wave = env.get('wave', {}) or {}
        restricted = env.get('restricted_water', {}) or {}

        wind_msg = Vector3()
        wind_msg.x = float(wind.get('speed_mps', 0.0))
        wind_msg.y = float(wind.get('direction_deg', 0.0))
        wind_msg.z = float(wind.get('anemometer_height_m', 10.0))
        self.wind_pub.publish(wind_msg)

        wave_msg = Vector3()
        wave_msg.x = float(wave.get('hs_m', 0.0))
        wave_msg.y = float(wave.get('tz_s', wave.get('Tz_s', 6.0)))
        wave_msg.z = math.radians(float(wave.get('direction_deg', 0.0)))
        self.wave_pub.publish(wave_msg)

        current_msg = OceanCurrents()
        current_msg.v_tide = float(current.get('tide_speed_mps', current.get('speed_mps', 0.0)))
        current_msg.dir_tide = float(current.get('tide_direction_deg', current.get('direction_deg', 0.0)))
        current_msg.v_wind = float(current.get('wind_drift_speed_mps', 0.0))
        current_msg.dir_wind = float(current.get('wind_drift_direction_deg', current_msg.dir_tide))
        current_msg.v_circ = float(current.get('circulation_speed_mps', 0.0))
        current_msg.dir_circ = float(current.get('circulation_direction_deg', current_msg.dir_tide))
        self.current_pub.publish(current_msg)

        if 'shallow_water_depth_m' in restricted:
            depth = Float64()
            depth.data = float(restricted['shallow_water_depth_m'])
            self.depth_pub.publish(depth)

    def _publish_faults(self):
        t = self._elapsed_s()
        failures = self.scenario.get('thrusters', {}).get('failures') or []
        for idx, fault in enumerate(failures):
            thruster_index = int(fault.get('index', -1))
            if thruster_index < 0:
                continue
            start_s = float(fault.get('start_s', 0.0))
            duration_s = float(fault.get('duration_s', 0.0))
            end_s = start_s + duration_s
            if t >= start_s and idx not in self.injected_faults:
                msg = Int32()
                msg.data = thruster_index
                self.fault_pub.publish(msg)
                self.injected_faults.add(idx)
                self.get_logger().warn(f'Injected thruster fault index={thruster_index}')
            if duration_s > 0 and t >= end_s and idx not in self.recovered_faults:
                msg = Int32()
                msg.data = -thruster_index
                self.fault_pub.publish(msg)
                self.recovered_faults.add(idx)
                self.get_logger().warn(f'Recovered thruster fault index={thruster_index}')

    def _publish_status(self):
        status = String()
        status.data = (
            f"{self.scenario.get('scenario_id', 'unknown')} "
            f"runtime t={self._elapsed_s():.1f}s"
        )
        self.status_pub.publish(status)

    def _on_timer(self):
        if self.publish_waypoints and self._elapsed_s() <= 5.0:
            self._publish_path()
        if self.publish_environment:
            self._publish_environment()
        if self.inject_faults:
            self._publish_faults()
        self._publish_status()


def main(args=None):
    rclpy.init(args=args)
    node = ScenarioRuntimePublisher()
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
