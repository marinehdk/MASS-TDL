"""ShipDynamicsNode — FCB 4-DOF MMG 模型 LifecycleNode @ 50Hz。

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
except ImportError:
    LifecycleNode = object
    State = object
    TransitionCallbackReturn = object

from .mmg_coefficients import MMGCoefficients
from .mmg_model import MMGModel, ShipState


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


class ShipDynamicsNode(LifecycleNode):
    """FCB 4-DOF MMG 动力学节点 — rclpy LifecycleNode。

    Lifecycle:
      configure → activate → deactivate → cleanup → shutdown
    """

    def __init__(self, node_name: str = "ship_dynamics_node"):
        if LifecycleNode is not object:
            super().__init__(node_name)
        else:
            self._node_name = node_name

        # 状态变量
        self._model: MMGModel | None = None
        self._state: ShipState = ShipState()

        # 命令缓存 (线程安全)
        self._delta_cmd: float = 0.0
        self._n_rps_cmd: float = 10.0
        self._wind_speed: float = 0.0
        self._wind_dir: float = 0.0
        self._current_speed: float = 0.0
        self._current_dir: float = 0.0
        self._cmd_lock = threading.Lock()

        # ROS2 句柄
        self._timer = None
        self._state_pub = None
        self._actuator_sub = None
        self._env_sub = None

        # 地理原点
        self._origin_lat_rad: float = 0.0
        self._origin_lon_rad: float = 0.0

    # ─── Lifecycle 回调 ───────────────────────────────────────

    def on_configure(self, state: State) -> TransitionCallbackReturn:
        """加载 MMG 参数，创建物理模型和初始状态。"""
        try:
            coeffs = self._load_coefficients()
            self._model = MMGModel(coeffs)
            self._state = ShipState(
                x=coeffs.x0, y=coeffs.y0, psi=coeffs.psi0, phi=0.0,
                u=coeffs.u0, v=0.0, r=0.0, p=0.0,
            )
            self._origin_lat_rad = math.radians(coeffs.origin_lat)
            self._origin_lon_rad = math.radians(coeffs.origin_lon)
            self._n_rps_cmd = coeffs.u0 / (coeffs.D_P * 3.0)  # 巡航转速估算
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

        # 发布 /sil/own_ship_state @ 50Hz
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

        self._timer = self.create_timer(0.02, self._step_callback)

        if hasattr(self, "get_logger"):
            self.get_logger().info("ShipDynamicsNode 已激活 (50Hz)")
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
        if hasattr(self, "get_logger"):
            self.get_logger().info("ShipDynamicsNode 已停用")
        return TransitionCallbackReturn.SUCCESS

    def on_cleanup(self, state: State) -> TransitionCallbackReturn:
        """重置模型状态。"""
        self._model = None
        self._state = ShipState()
        if hasattr(self, "get_logger"):
            self.get_logger().info("ShipDynamicsNode 已清理")
        return TransitionCallbackReturn.SUCCESS

    # ─── 回调 ─────────────────────────────────────────────────

    def _actuator_callback(self, msg):
        """接收舵角/转速指令。"""
        with self._cmd_lock:
            self._delta_cmd = getattr(msg, "rudder_angle", 0.0)
            self._n_rps_cmd = getattr(msg, "throttle", 0.0) * 20.0

    def _environment_callback(self, msg):
        """接收环境状态。"""
        with self._cmd_lock:
            self._wind_speed = getattr(msg, "wind_speed_mps", 0.0)
            self._wind_dir = math.radians(getattr(msg, "wind_direction", 0.0))
            self._current_speed = getattr(msg, "current_speed_mps", 0.0)
            self._current_dir = math.radians(getattr(msg, "current_direction", 0.0))

    def _step_callback(self):
        """50Hz 推进步 — RK4 积分 + 发布 OwnShipState。"""
        if self._model is None:
            return

        with self._cmd_lock:
            dc = self._delta_cmd
            nr = self._n_rps_cmd
            ws = self._wind_speed
            wd = self._wind_dir
            cs = self._current_speed
            cd = self._current_dir

        self._state = self._model.rk4_step(
            self._state, dc, nr, ws, wd, cs, cd,
        )

        lat = self._origin_lat_rad + math.radians(
            _lat_offset(self._state.y, self._origin_lat_rad)
        )
        lon = self._origin_lon_rad + math.radians(
            _lon_offset(self._state.x, lat)
        )

        msg = self._make_msg()
        msg.stamp = self.get_clock().now().to_msg()
        msg.lat = math.degrees(lat)
        msg.lon = math.degrees(lon)
        msg.heading = self._state.psi
        msg.sog = math.sqrt(self._state.u ** 2 + self._state.v ** 2)
        msg.cog = math.atan2(self._state.v, self._state.u)
        msg.rot = self._state.r
        msg.u = self._state.u
        msg.v = self._state.v
        msg.r = self._state.r
        msg.rudder_angle = dc
        msg.throttle = nr / 20.0

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
