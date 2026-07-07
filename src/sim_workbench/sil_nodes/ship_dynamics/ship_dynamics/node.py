"""ShipDynamicsNode — FCB 4-DOF MMG 模型 LifecycleNode @ 50Hz integration.

发布: /sil/own_ship_state (sil_msgs/OwnShipState)
订阅: /sil/actuator_cmd (sil_msgs/OwnShipState — rudder_angle + throttle)
      /sil/environment  (sil_msgs/EnvironmentState)
"""

import math
import threading

try:
    import rclpy
    from rclpy.lifecycle import LifecycleNode, State, TransitionCallbackReturn
    from rclpy.qos import QoSProfile, QoSReliabilityPolicy, QoSDurabilityPolicy, QoSHistoryPolicy
    from rclpy.time import Time
    from rclpy.duration import Duration
except ImportError:
    LifecycleNode = object
    State = object
    TransitionCallbackReturn = object
    Duration = object

from .mmg_coefficients import MMGCoefficients
from .mmg_model import MMGModel, ShipState


OWN_SHIP_STATE_PUBLISH_PERIOD_S = 0.1


def _lat_offset(meters: float, lat_ref_rad: float) -> float:
    """将 NED y (meters) 转换为纬度偏移 (deg)。

    1° 纬度 ≈ 111120 m (WGS84 标准)。
    """
    return meters / 111120.0


def _lon_offset(meters: float, lat_rad: float) -> float:
    """将 NED x (meters) 转换为经度偏移 (deg)。

    1° 经度 ≈ 111120 * cos(latitude) m。
    """
    cos_lat = math.cos(lat_rad)
    if abs(cos_lat) < 1e-10:
        cos_lat = 1e-10
    return meters / (111120.0 * cos_lat)


def _normalize_angle_rad(angle_rad: float) -> float:
    """Normalize an angle to [0, 2π)."""
    return angle_rad % (2.0 * math.pi)


def _math_heading_to_nav_heading(psi_rad: float) -> float:
    """Convert MMG math heading to nautical heading.

    MMG uses ψ=0 along +x and positive counter-clockwise. The SIL/HMI/L3
    boundary uses nautical bearing: 0=north, positive clockwise.
    """
    return _normalize_angle_rad((math.pi / 2.0) - psi_rad)


def _ground_track_to_nav_cog(psi_rad: float, u_mps: float, v_mps: float) -> float:
    """Return nautical COG from body-frame velocity and MMG heading."""
    east_mps = u_mps * math.cos(psi_rad) - v_mps * math.sin(psi_rad)
    north_mps = u_mps * math.sin(psi_rad) + v_mps * math.cos(psi_rad)
    if math.hypot(east_mps, north_mps) < 1e-9:
        return _math_heading_to_nav_heading(psi_rad)
    return _normalize_angle_rad(math.atan2(east_mps, north_mps))


class ShipDynamicsNode(LifecycleNode):
    """FCB 4-DOF MMG 动力学节点 — rclpy LifecycleNode。

    Lifecycle:
      configure → activate → deactivate → cleanup → shutdown
    """

    def __init__(self, node_name: str = "ship_dynamics_node"):
        if LifecycleNode is not object:
            try:
                from rclpy.parameter import Parameter
                overrides = [Parameter('use_sim_time', Parameter.Type.BOOL, True)]
            except ImportError:
                overrides = None

            import inspect
            kwargs = {}
            try:
                sig = inspect.signature(super().__init__)
                if "parameter_overrides" in sig.parameters and overrides is not None:
                    kwargs["parameter_overrides"] = overrides
                if "allow_undeclared_parameters" in sig.parameters:
                    kwargs["allow_undeclared_parameters"] = True
                if "automatically_declare_parameters_from_overrides" in sig.parameters:
                    kwargs["automatically_declare_parameters_from_overrides"] = True
            except Exception:
                pass

            super().__init__(node_name, **kwargs)
        else:
            self._node_name = node_name

        # 状态变量
        self._model: MMGModel | None = None
        self._state: ShipState = ShipState()

        # 命令缓存 (线程安全)
        self._delta_cmd: float = 0.0
        # _n_rps_cmd 初始为 0.0；在 on_configure 时设置为巡航转速。
        # 切勿将此处硬编码为非零常数 (如旧值 10.0 rps 会导致船船失控加速).
        self._n_rps_cmd: float = 0.0
        self._wind_speed: float = 0.0
        self._wind_dir: float = 0.0
        self._current_speed: float = 0.0
        self._current_dir: float = 0.0
        self._cmd_lock = threading.Lock()

        # Fix #8: avoidance plan heading override. SIL 环境中无 GNC guidance 层将
        # M5 avoidance plan 转为 actuator 命令。ship_dynamics 直接订阅
        # /l3/m5/avoidance_plan，提取第一个有效 waypoint 作为 heading 目标。
        self._avoidance_heading_rad: float | None = None
        self._avoidance_speed_mps: float | None = None
        self._avoidance_sub = None

        # ROS2 句柄
        self._timer = None
        self._state_pub = None
        self._actuator_sub = None
        self._env_sub = None
        self._last_sim_time = None
        self._last_publish_sim_ns = None

        # 地理原点
        self._origin_lat_rad: float = 0.0
        self._origin_lon_rad: float = 0.0

        # Legacy field retained for older tests/config; ROS2 truth publishing is sim-time driven.
        self._last_pub_wall_time: float = 0.0

        # Legacy parameter retained for config compatibility.
        self._headless: bool = False


    # ─── Lifecycle 回调 ───────────────────────────────────────

    def on_configure(self, state: State) -> TransitionCallbackReturn:
        """加载 MMG 参数，创建物理模型 and 初始状态。"""
        try:
            coeffs = self._load_coefficients()
            self._model = MMGModel(coeffs)
            self._state = ShipState(
                x=coeffs.x0, y=coeffs.y0, psi=coeffs.psi0, phi=0.0,
                u=coeffs.u0, v=0.0, r=0.0, p=0.0,
            )
            self._origin_lat_rad = math.radians(coeffs.origin_lat)
            self._origin_lon_rad = math.radians(coeffs.origin_lon)

            # 使用 coeffs.n_rps_cruise 属性 (与 X_uu 标定一致的巡航转速)
            try:
                self.declare_parameter("n_rps_initial", coeffs.n_rps_cruise)
                self._n_rps_cmd = self.get_parameter("n_rps_initial").value
            except Exception:
                try:
                    self._n_rps_cmd = self.get_parameter("n_rps_initial").value
                except Exception:
                    self._n_rps_cmd = coeffs.n_rps_cruise

            # Headless parameters
            try:
                self.declare_parameter("headless", False)
                self._headless = self.get_parameter("headless").value
            except Exception:
                self._headless = False
        except Exception as exc:
            if hasattr(self, "get_logger"):
                self.get_logger().error(f"on_configure 失败: {exc}")
            return TransitionCallbackReturn.FAILURE

        if hasattr(self, "get_logger"):
            self.get_logger().info("MMG 模型已加载")
        return TransitionCallbackReturn.SUCCESS

    def on_activate(self, state: State) -> TransitionCallbackReturn:
        """激活发布器、订阅器和 50Hz 定时器。"""
        if self._model is None:
            if hasattr(self, "get_logger"):
                self.get_logger().error("on_activate: 模型未配置，请先调用 configure")
            return TransitionCallbackReturn.FAILURE

        qos_sensor = QoSProfile(
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.VOLATILE,
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=1,
        )

        # 发布 /sil/own_ship_state @ fixed sim-time cadence
        self._state_pub = self.create_publisher(
            msg_type=self._get_own_ship_state_msg(),
            topic="/sil/own_ship_state",
            qos_profile=qos_sensor,
        )

        # 订阅 /sil/actuator_cmd
        self._actuator_sub = self.create_subscription(
            msg_type=self._get_own_ship_state_msg(),
            topic="/sil/actuator_cmd",
            callback=self._actuator_callback,
            qos_profile=qos_sensor,
        )

        # 订阅 /sil/environment
        self._env_sub = self.create_subscription(
            msg_type=self._get_environment_state_msg(),
            topic="/sil/environment",
            callback=self._environment_callback,
            qos_profile=qos_sensor,
        )

        # Fix #8: subscribe to M5 avoidance plan for heading override.
        # In the SIL default profile, no GNC guidance layer converts avoidance
        # plans to actuator commands. ship_dynamics bridges this gap directly:
        # extract the first waypoint with displacement > 1 m as heading target.
        try:
            from l3_msgs.msg import AvoidancePlan
            self._avoidance_sub = self.create_subscription(
                AvoidancePlan,
                "/l3/m5/avoidance_plan",
                self._on_avoidance_plan,
                qos_sensor,
            )
        except ImportError:
            self.get_logger().warn("l3_msgs not available — avoidance plan override disabled")

        self._timer = self.create_timer(self._model.c.dt, self._step_callback)

        if hasattr(self, "get_logger"):
            self.get_logger().info("ShipDynamicsNode 已激活 (50Hz integration, 10Hz sim publish)")
        return TransitionCallbackReturn.SUCCESS

    def on_deactivate(self, state: State) -> TransitionCallbackReturn:
        """销毁定时器、发布器和订阅器。"""
        if self._timer is not None:
            self.destroy_timer(self._timer)
            self._timer = None
        if self._state_pub is not None:
            self.destroy_publisher(self._state_pub)
            self._state_pub = None
        if self._actuator_sub is not None:
            self.destroy_subscription(self._actuator_sub)
            self._actuator_sub = None
        if self._env_sub is not None:
            self.destroy_subscription(self._env_sub)
            self._env_sub = None
        self._last_sim_time = None
        self._last_publish_sim_ns = None
        if hasattr(self, "get_logger"):
            self.get_logger().info("ShipDynamicsNode 已停用")
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state: State) -> TransitionCallbackReturn:
        """重置模型状态。"""
        self._model = None
        self._state = ShipState()
        self._last_sim_time = None
        self._last_publish_sim_ns = None
        with self._cmd_lock:
            self._delta_cmd = 0.0
            self._n_rps_cmd = 0.0
            self._wind_speed = 0.0
            self._wind_dir = 0.0
            self._current_speed = 0.0
            self._current_dir = 0.0
        if hasattr(self, "get_logger"):
            self.get_logger().info("ShipDynamicsNode 已清理")
        return TransitionCallbackReturn.SUCCESS

    # ─── 回调 ─────────────────────────────────────────────────

    def _actuator_callback(self, msg):
        """接收舵角/转速指令。

        通过对称的油门-转速映射，消除 L3 桥接层与水动力模型之间的阻力平衡偏差。
        """
        with self._cmd_lock:
            self._delta_cmd = getattr(msg, "rudder_angle", 0.0)
            throttle = getattr(msg, "throttle", 0.0)
            if self._model is not None and throttle > 1e-6:
                c = self._model.c
                # 油门反归一化为目标航速 (最大对应 25.0 节，与 sil_topic_bridge 中的 MAX_SPEED_KN 保持一致)
                u_target = throttle * (25.0 * 0.514444)
                # 使用线性比例换算螺旋桨转速以克服水阻力维持目标航速 (根据标定点 u0=5.144 m/s -> n_rps=3.0)
                self._n_rps_cmd = u_target * (c.n_rps_cruise / c.u0)
            else:
                self._n_rps_cmd = 0.0

    def _environment_callback(self, msg):
        """接收环境状态。"""
        with self._cmd_lock:
            self._wind_speed = getattr(msg, "wind_speed_mps", 0.0)
            self._wind_dir = math.radians(getattr(msg, "wind_direction", 0.0))
            self._current_speed = getattr(msg, "current_speed_mps", 0.0)
            self._current_dir = math.radians(getattr(msg, "current_direction", 0.0))

    def _on_avoidance_plan(self, msg):
        """Fix #8: 从 M5 avoidance plan 提取 heading/speed 目标。"""
        if not hasattr(msg, "waypoints") or not msg.waypoints:
            return
        # 跳过 anchor (ownship 同位置) 等距离 < 1m 的 waypoint
        for wp in msg.waypoints:
            lat = getattr(wp.position, "latitude", 0.0)
            lon = getattr(wp.position, "longitude", 0.0)
            dlat = lat - math.degrees(self._origin_lat_rad)
            dlon = lon - math.degrees(self._origin_lon_rad)
            dy = dlat * 111120.0  # m per deg lat
            dx = dlon * 111120.0 * math.cos(self._origin_lat_rad)
            dist = math.hypot(dx - self._state.x, dy - self._state.y)
            if dist > 1.0:
                with self._cmd_lock:
                    self._avoidance_heading_rad = math.atan2(dy, dx)
                    self._avoidance_speed_mps = (
                        getattr(wp, "target_speed_kn", 7.0) * 0.514444
                    )
                return
        # 所有 waypoint 都在 1m 内 → 清除 override
        with self._cmd_lock:
            self._avoidance_heading_rad = None
            self._avoidance_speed_mps = None

    def _step_callback(self):
        """50Hz 推进步 — RK4 积分 + 发布 OwnShipState。"""
        if self._model is None:
            return

        now_sim = self.get_clock().now()
        if self._last_sim_time is None:
            self._last_sim_time = now_sim
            return

        dt = self._model.c.dt  # single source of truth — never hardcode
        dt_ns = int(dt * 1e9)
        elapsed_ns = (now_sim - self._last_sim_time).nanoseconds
        if elapsed_ns < dt_ns:
            return

        steps = elapsed_ns // dt_ns

        with self._cmd_lock:
            dc = self._delta_cmd
            nr = self._n_rps_cmd
            ws = self._wind_speed
            wd = self._wind_dir
            cs = self._current_speed
            cd = self._current_dir
            # Fix #8: override rudder from avoidance plan heading target.
            # Simple P-controller: heading error → rudder angle.
            if self._avoidance_heading_rad is not None:
                psi_err = self._avoidance_heading_rad - self._state.psi
                # wrap to [-π, +π]
                while psi_err > math.pi:
                    psi_err -= 2.0 * math.pi
                while psi_err < -math.pi:
                    psi_err += 2.0 * math.pi
                dc = max(-0.6108, min(0.6108, 1.5 * psi_err))  # ±35° clamp
                if self._avoidance_speed_mps is not None:
                    # Adjust RPM for target speed
                    c = self._model.c
                    nr = self._avoidance_speed_mps * (c.n_rps_cruise / c.u0)

        for _ in range(steps):
            self._state = self._model.rk4_step(
                self._state, dc, nr, ws, wd, cs, cd,
            )
            self._last_sim_time += Duration(nanoseconds=dt_ns)
            if self._should_publish_state(self._last_sim_time):
                self._publish_state(self._last_sim_time, dc, nr)

    def _should_publish_state(self, stamp) -> bool:
        period_ns = int(OWN_SHIP_STATE_PUBLISH_PERIOD_S * 1e9)
        stamp_ns = self._stamp_nanoseconds(stamp)
        if self._last_publish_sim_ns is None:
            self._last_publish_sim_ns = stamp_ns
            return True
        elapsed_ns = stamp_ns - self._last_publish_sim_ns
        if elapsed_ns >= period_ns:
            self._last_publish_sim_ns = stamp_ns
            return True
        return False

    @staticmethod
    def _stamp_nanoseconds(stamp) -> int:
        if hasattr(stamp, "nanoseconds"):
            return int(stamp.nanoseconds)
        return int(getattr(stamp, "_ns", 0))

    def _publish_state(self, stamp, dc: float, nr: float) -> None:
        if self._model is None or self._state_pub is None:
            return
        lat = self._origin_lat_rad + math.radians(
            _lat_offset(self._state.y, self._origin_lat_rad)
        )
        lon = self._origin_lon_rad + math.radians(
            _lon_offset(self._state.x, lat)
        )

        msg = self._make_msg()
        msg.stamp = stamp.to_msg()
        msg.lat = math.degrees(lat)
        msg.lon = math.degrees(lon)
        msg.heading = _math_heading_to_nav_heading(self._state.psi)
        msg.sog = math.sqrt(self._state.u ** 2 + self._state.v ** 2)
        msg.cog = _ground_track_to_nav_cog(self._state.psi, self._state.u, self._state.v)
        msg.rot = -self._state.r  # MMG r: CCW positive; maritime ROT: CW (starboard) positive
        msg.u = self._state.u
        msg.v = self._state.v
        msg.r = self._state.r
        msg.rudder_angle = dc
        msg.throttle = (nr * (self._model.c.u0 / self._model.c.n_rps_cruise)) / (25.0 * 0.514444)
        self._state_pub.publish(msg)


    # ─── 辅助方法 ─────────────────────────────────────────────

    def _load_coefficients(self) -> MMGCoefficients:
        """从 ROS 参数服务器加载 MMG 系数 (fallback: 默认值)。"""
        coeffs = MMGCoefficients()
        param_map = {
            "L": "L", "d": "d", "B": "B",
            "displacement_t": "displacement_t", "x_G": "x_G",
            "m_x_prime": "m_x_prime", "m_y_prime": "m_y_prime",
            "J_zz_prime": "J_zz_prime",
            "X_vv": "X_vv", "X_vr": "X_vr", "X_rr": "X_rr", "X_vvvv": "X_vvvv",
            "Y_v": "Y_v", "Y_r": "Y_r",
            "Y_vvv": "Y_vvv", "Y_vvr": "Y_vvr", "Y_vrr": "Y_vrr", "Y_rrr": "Y_rrr",
            "N_v": "N_v", "N_r": "N_r",
            "N_vvv": "N_vvv", "N_vvr": "N_vvr", "N_vrr": "N_vrr", "N_rrr": "N_rrr",
            "G_M": "G_M", "T_phi": "T_phi",
            "t_P": "t_P", "w_P": "w_P", "D_P": "D_P",
            "k_0": "k_0", "k_1": "k_1", "k_2": "k_2",
            "t_R": "t_R", "a_H": "a_H",
            "x_H_prime": "x_H_prime", "x_R_prime": "x_R_prime",
            "gamma_R": "gamma_R", "l_R_prime": "l_R_prime",
            "kappa": "kappa", "epsilon": "epsilon",
            "A_R": "A_R", "f_alpha": "f_alpha",
            "dt": "dt",
            "x0": "x0", "y0": "y0", "psi0": "psi0", "u0": "u0",
            "origin_lat": "origin_lat", "origin_lon": "origin_lon",
        }
        for py_attr, ros_param in param_map.items():
            try:
                if hasattr(self, "get_parameter") and not hasattr(self, "declare_parameter"):
                    pass  # avoid declare_parameter on non-LifecycleNode
                elif hasattr(self, "get_parameter"):
                    declared = self.declare_parameter(ros_param, getattr(coeffs, py_attr))
                    setattr(coeffs, py_attr, declared.value)
            except Exception:
                try:
                    if hasattr(self, "get_parameter"):
                        setattr(coeffs, py_attr, self.get_parameter(ros_param).value)
                except Exception:
                    pass
        return coeffs

    def _get_own_ship_state_msg(self):
        """延迟导入 OwnShipState 消息类型。"""
        try:
            from sil_msgs.msg import OwnShipState
            return OwnShipState
        except ImportError:
            class _FallbackMsg:
                stamp = None
                lat = 0.0; lon = 0.0; heading = 0.0
                sog = 0.0; cog = 0.0; rot = 0.0
                u = 0.0; v = 0.0; r = 0.0
                rudder_angle = 0.0; throttle = 0.0
            return _FallbackMsg

    def _get_environment_state_msg(self):
        """延迟导入 EnvironmentState 消息类型。"""
        try:
            from sil_msgs.msg import EnvironmentState
            return EnvironmentState
        except ImportError:
            class _FallbackMsg:
                stamp = None
                wind_direction = 0.0; wind_speed_mps = 0.0
                current_direction = 0.0; current_speed_mps = 0.0
                visibility_nm = 10.0; sea_state_beaufort = 0
            return _FallbackMsg

    def _make_msg(self):
        """创建消息实例。"""
        msg_cls = self._get_own_ship_state_msg()
        return msg_cls()


def main():
    try:
        rclpy.init(args=None)
    except Exception:
        pass

    node = ShipDynamicsNode()
    try:
        rclpy.spin(node)
    except Exception:
        pass
    finally:
        try:
            node.destroy_node()
            rclpy.shutdown()
        except Exception:
            pass


if __name__ == "__main__":
    main()
