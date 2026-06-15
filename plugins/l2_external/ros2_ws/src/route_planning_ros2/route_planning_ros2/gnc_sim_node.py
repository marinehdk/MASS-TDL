"""
gnc_sim_node.py - ROS2桥接节点

发布:
  /route_planning/route_plan (RoutePlan, coordinate_transform_node接收)
  /route_planning/gnc_route_plan (GncRoutePlan, 避障模块接收)
订阅:
  /ship/geo_position (coordinate_transform_node从/ship/odometry反算的WGS84经纬度)
  /ship/odometry (仅记录NED轨迹日志)

共享文件:
  gnc_bridge_route.json  (gnc_sim.py 写入, 航线航点)
  gnc_bridge_state.json  (本节点写入, 最新船位WGS84经纬度)

数据流:
  gnc_sim.py → gnc_bridge_route.json → 本节点 → RoutePlan → coordinate_transform_node
  动力学 → /ship/odometry → coordinate_transform_node → /ship/geo_position → 本节点
  本节点 → gnc_bridge_state.json → gnc_sim.py(_poll_bridge_state) → 前端
"""
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSDurabilityPolicy, QoSReliabilityPolicy
from nav_msgs.msg import Odometry
from ship_interfaces.msg import RoutePlan, GeoPosition
try:
    from ship_interfaces.msg import GncRoutePlan
except ImportError:
    GncRoutePlan = None
import json
import math
import os
import time
from datetime import datetime, timezone

# ── 共享文件路径 (设置环境变量 GNC_ROUTE_PLANNING_DIR 可覆盖默认路径) ──
_GNC_RP_DIR = os.environ.get('GNC_ROUTE_PLANNING_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', '..', '航线规划'))
_GNC_RP_DIR = os.path.normpath(_GNC_RP_DIR)
SHARED_STATE_FILE = os.path.join(_GNC_RP_DIR, 'gnc_bridge_state.json')
SHARED_ROUTE_FILE = os.path.join(_GNC_RP_DIR, 'gnc_bridge_route.json')
FEEDBACK_LOG_DIR = os.environ.get(
    'SHIP_FEEDBACK_LOG_DIR',
    '/home/mass/simulation/船舶动力学/gnc_ws/ship_feedback_logs')
ROUTE_PLAN_LOG = os.path.join(FEEDBACK_LOG_DIR, 'route_plan_log.txt')
RAW_ROUTE_PUBLISH_CSV = os.path.join(FEEDBACK_LOG_DIR, 'raw_route_publish_log.csv')
RAW_ROUTE_PUBLISH_TXT = os.path.join(FEEDBACK_LOG_DIR, 'raw_route_publish_log.txt')
ACTUAL_TRACK_LOG = os.path.join(_GNC_RP_DIR, 'actual_track_log.txt')


def _append_csv_header_if_needed(path, header):
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        with open(path, 'a', encoding='utf-8') as f:
            f.write(header + '\n')


def _append_raw_route_publish_log(source, key, route_id, route_pts):
    try:
        os.makedirs(FEEDBACK_LOG_DIR, exist_ok=True)
        ts = datetime.now(timezone.utc).isoformat()
        header = ('timestamp,source,route_id,route_key,waypoint,waypoint_count,'
                  'lat,lon,speed_limit_mps,speed_kn,heading_deg,cum_dist_km')
        _append_csv_header_if_needed(RAW_ROUTE_PUBLISH_CSV, header)
        with open(RAW_ROUTE_PUBLISH_CSV, 'a', encoding='utf-8') as csvf, \
                open(RAW_ROUTE_PUBLISH_TXT, 'a', encoding='utf-8') as txtf:
            count = len(route_pts)
            for i, p in enumerate(route_pts):
                lat = float(p['lat'])
                lon = float(p['lon'])
                speed_kn = float(p.get('speed_kn', 0) or 0)
                speed_limit_mps = float(p.get('speed_limit_mps', speed_kn * 0.5144) or 0)
                hdg = float(p.get('heading_deg', 0) or 0)
                cum = float(p.get('cum_dist_km', 0) or 0)
                csvf.write(
                    f'{ts},{source},{route_id},{key},{i},{count},'
                    f'{lat:.8f},{lon:.8f},{speed_limit_mps:.3f},{speed_kn:.3f},'
                    f'{hdg:.2f},{cum:.3f}\n')
                txtf.write(
                    f'{ts} source={source} route_id={route_id} route={key} '
                    f'waypoint={i}/{max(count - 1, 0)} lat={lat:.8f} lon={lon:.8f} '
                    f'speed_limit={speed_limit_mps:.3f}m/s speed_kn={speed_kn:.3f} '
                    f'hdg={hdg:.2f} cum_dist={cum:.3f}km\n')
    except Exception as exc:
        print(f'[gnc_sim_node] write raw route publish log failed: {exc}')


class GncSimNode(Node):
    def __init__(self):
        super().__init__('gnc_sim_node')

        self.route_plan_msg = None
        self.gnc_route_plan_msg = None
        self.current_route_id = None
        self._track_file = None

        route_qos = QoSProfile(
            depth=10,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
            reliability=QoSReliabilityPolicy.RELIABLE
        )

        # ── 发布者1: RoutePlan → coordinate_transform_node ──
        self.route_plan_pub = self.create_publisher(
            RoutePlan, '/route_planning/route_plan', route_qos)

        # GncRoutePlan is optional in the current ship_interfaces build.
        self.gnc_route_plan_pub = None
        if GncRoutePlan is not None:
            self.gnc_route_plan_pub = self.create_publisher(
                GncRoutePlan, '/route_planning/gnc_route_plan', route_qos)
        else:
            self.get_logger().warn(
                'GncRoutePlan message not available; /route_planning/gnc_route_plan disabled')

        # ── 订阅者1: /ship/geo_position (同事的WGS84坐标，不做任何变换) ──
        self.geo_pos_sub = self.create_subscription(
            GeoPosition, '/ship/geo_position', self.geo_pos_callback, 10)

        # ── 订阅者2: /ship/odometry (仅记录NED轨迹日志) ──
        self.odom_sub = self.create_subscription(
            Odometry, '/ship/odometry', self.odom_callback, 10)

        # ── 定时器 ──
        self.publish_timer = self.create_timer(1.0, self.timer_callback)
        self.route_check_timer = self.create_timer(0.5, self.check_route_update)

        self.get_logger().info('gnc_sim_node 已启动 (订阅GeoPosition, 写入gnc_bridge_state.json)')

    def check_route_update(self):
        if not os.path.exists(SHARED_ROUTE_FILE):
            return
        try:
            with open(SHARED_ROUTE_FILE, 'r') as f:
                data = json.load(f)
            route_pts = data.get('sample_points', [])
            if not route_pts:
                return
            new_id = data.get('route_id')
            if new_id == self.current_route_id and self.route_plan_msg is not None:
                return

            # ── 构造 RoutePlan → coordinate_transform_node ──
            route_plan = RoutePlan()
            route_plan.header.stamp = self.get_clock().now().to_msg()
            route_plan.header.frame_id = 'WGS84'
            route_plan.route_id = data.get('route_id', new_id)
            route_plan.route_type = data.get('route_type', 'transit')
            for p in route_pts:
                route_plan.latitude.append(float(p['lat']))
                route_plan.longitude.append(float(p['lon']))
                route_plan.speed_limit_mps.append(float(p.get('speed_kn', 0) * 0.5144))
                route_plan.navigation_mode.append('')
            self.route_plan_msg = route_plan

            # ── 构造 GncRoutePlan → 避障模块 ──
            self.gnc_route_plan_msg = None
            if GncRoutePlan is not None:
                gnc_plan = GncRoutePlan()
                gnc_plan.header.stamp = self.get_clock().now().to_msg()
                gnc_plan.header.frame_id = 'WGS84'
                gnc_plan.total_waypoints = len(route_pts)
                for p in route_pts:
                    gnc_plan.latitude.append(float(p['lat']))
                    gnc_plan.longitude.append(float(p['lon']))
                self.gnc_route_plan_msg = gnc_plan

            self.current_route_id = new_id
            self.get_logger().info(
                f'已加载新航线: {len(route_pts)} 航点, '
                f'route_id={route_plan.route_id}')
            # 立即发布
            self.route_plan_pub.publish(self.route_plan_msg)
            if self.gnc_route_plan_pub is not None and self.gnc_route_plan_msg is not None:
                self.gnc_route_plan_pub.publish(self.gnc_route_plan_msg)
            _append_raw_route_publish_log(
                'gnc_sim_node.check_route_update',
                data.get('selected_key', ''),
                route_plan.route_id,
                route_pts)

            # ── 写入路径规划所有点经纬度到 route_plan_log.txt ──
            try:
                ts = datetime.now(timezone.utc).isoformat()
                os.makedirs(FEEDBACK_LOG_DIR, exist_ok=True)
                with open(ROUTE_PLAN_LOG, 'w') as f:
                    for i, p in enumerate(route_pts):
                        lat = float(p['lat'])
                        lon = float(p['lon'])
                        hdg = p.get('heading_deg', 0)
                        spd_kn = float(p.get('speed_kn', 0) or 0)
                        speed_limit_mps = float(p.get('speed_limit_mps', spd_kn * 0.5144) or 0)
                        cum = float(p.get('cum_dist_km', 0) or 0)
                        f.write(f'{ts} route_id={route_plan.route_id} lat={lat:.8f} lon={lon:.8f} '
                                f'hdg={float(hdg):.2f} speed_limit={speed_limit_mps:.3f}m/s '
                                f'speed_kn={spd_kn:.3f} '
                                f'cum_dist={cum:.2f}km '
                                f'waypoint={i}/{len(route_pts)-1}\n')
            except Exception:
                pass

            # ── 打开实际轨迹日志文件 ──
            try:
                if self._track_file is not None:
                    self._track_file.close()
                self._track_file = open(ACTUAL_TRACK_LOG, 'w')
            except Exception:
                self._track_file = None

        except Exception as e:
            self.get_logger().warn(f'读取航线文件失败: {e}')

    def timer_callback(self):
        if self.route_plan_msg is not None:
            self.route_plan_pub.publish(self.route_plan_msg)
        if self.gnc_route_plan_pub is not None and self.gnc_route_plan_msg is not None:
            self.gnc_route_plan_pub.publish(self.gnc_route_plan_msg)

    def geo_pos_callback(self, msg):
        """订阅 /ship/geo_position — 同事coordinate_transform_node发布的WGS84坐标。
        直接使用同事的经纬度、航向、速度，不做任何坐标变换，写入gnc_bridge_state.json。"""
        timestamp = datetime.now(timezone.utc).isoformat()

        state = {
            'latitude':       msg.latitude,
            'longitude':      msg.longitude,
            'heading_deg':    msg.heading_deg,
            'course_deg':     msg.course_deg,
            'speed_mps':      msg.speed_mps,
            'speed_kn':       msg.speed_mps * 1.94384,
            'surge_mps':      msg.surge_mps,
            'sway_mps':       msg.sway_mps,
            'yaw_rate_dps':   msg.yaw_rate_deg_s,
            'vel_north_mps':  msg.vel_north_mps,
            'vel_east_mps':   msg.vel_east_mps,
            'x_ned':          msg.x_ned,
            'y_ned':          msg.y_ned,
            'nav_state':      msg.nav_state,
            'nav_state_str':  msg.nav_state_str,
            'origin_locked':  msg.origin_locked,
            'origin_lat':     msg.origin_lat,
            'origin_lon':     msg.origin_lon,
            'x_local':        msg.x_ned,
            'y_local':        msg.y_ned,
            'mode':           'simulation',
            'timestamp':      timestamp
        }

        # 原子写入 gnc_bridge_state.json (write to .tmp then rename)
        try:
            tmp_path = SHARED_STATE_FILE + '.tmp'
            with open(tmp_path, 'w') as f:
                json.dump(state, f)
            os.replace(tmp_path, SHARED_STATE_FILE)
        except Exception:
            pass

        # 写实际轨迹日志
        if self._track_file is not None:
            try:
                self._track_file.write(
                    f'{timestamp} lat={msg.latitude:.8f} lon={msg.longitude:.8f} '
                    f'hdg={msg.heading_deg:.2f} spd={msg.speed_mps:.3f}m/s '
                    f'x_ned={msg.x_ned:.2f} y_ned={msg.y_ned:.2f} '
                    f'origin=({msg.origin_lat:.8f},{msg.origin_lon:.8f})\n')
                self._track_file.flush()
            except Exception:
                pass

    def odom_callback(self, msg):
        """订阅 /ship/odometry，仅记录NED轨迹日志（备用）。"""
        if self._track_file is not None:
            try:
                x = msg.pose.pose.position.x
                y = msg.pose.pose.position.y
                vx = msg.twist.twist.linear.x
                vy = msg.twist.twist.linear.y
                speed_mps = math.sqrt(vx * vx + vy * vy)
                ts = datetime.now(timezone.utc).isoformat()
                self._track_file.write(
                    f'{ts} x_ned={x:.2f} y_ned={y:.2f} '
                    f'spd={speed_mps:.3f}m/s\n')
            except Exception:
                pass


def main(args=None):
    rclpy.init(args=args)
    node = GncSimNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node._track_file is not None:
            node._track_file.close()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
