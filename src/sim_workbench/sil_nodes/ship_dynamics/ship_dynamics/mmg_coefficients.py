"""MMG 系数数据类 — FCB 45m 船型参数 (Yasukawa & Yoshimura 2015)。

所有参数从 fcb_dynamics.yaml 加载，严禁在 Python 中硬编码。
"""

from dataclasses import dataclass, field
import math


@dataclass(frozen=False)
class MMGCoefficients:
    """Yasukawa 2015 MMG 模型全系数集。

    Attributes:
        L: 船长 (m)
        d: 吃水 (m)
        B: 型宽 (m)
        displacement_t: 排水量 (tonnes)
        x_G: 重心纵向位置 (m, 舯后为正)
        rho: 海水密度 (kg/m³)
        m_x_prime: 纵向附加质量系数 (无因次)
        m_y_prime: 横向附加质量系数 (无因次)
        J_zz_prime: 艏摇附加惯矩系数 (无因次)
        X_vv ~ X_rr: 纵荡 Abkowitz 导数
        Y_v ~ Y_rrr: 横荡 Abkowitz 导数
        N_v ~ N_rrr: 艏摇 Abkowitz 导数
        G_M: 初稳心高度 (m)
        T_phi: 横摇固有周期 (s)
        t_P: 推力减额系数
        w_P: 伴流分数
        D_P: 螺旋桨直径 (m)
        k_0, k_1, k_2: K_T 曲线系数 (J² 拟合)
        t_R: 舵推力减额系数
        a_H: 舵横向力修正因子
        x_H_prime: 舵横向力作用点 (无因次)
        x_R_prime: 舵纵向位置 (无因次)
        gamma_R: 舵整流系数
        l_R_prime: 舵横向力臂 (无因次)
        kappa: K_T 曲线常数
        epsilon: 舵展弦比系数
        A_R: 舵面积 (m²)
        f_alpha: 舵升力系数斜率
        dt: RK4 积分步长 (s)
        x0, y0, psi0, u0: 初始状态
        origin_lat, origin_lon: NED→WGS84 地理原点
    """

    # ===== 船型参数 =====
    L: float = 46.0
    d: float = 2.8
    B: float = 8.0
    displacement_t: float = 450.0
    x_G: float = 0.0
    rho: float = 1025.0

    # ===== 附加质量 (无因次) =====
    m_x_prime: float = 0.00831
    m_y_prime: float = 0.1284
    J_zz_prime: float = 0.00676

    # ===== 裸船体导数 (Abkowitz 多项式) =====
    # 纵荡 X
    X_vv: float = -0.0407
    X_vr: float = 0.0441
    X_rr: float = 0.0127
    X_vvvv: float = -0.0607

    # 横荡 Y
    Y_v: float = -0.3073
    Y_r: float = 0.1521
    Y_vvv: float = -0.7256
    Y_vvr: float = -0.1338
    Y_vrr: float = 0.1657
    Y_rrr: float = -0.0303

    # 艏摇 N
    N_v: float = -0.1084
    N_r: float = -0.0585
    N_vvv: float = 0.0040
    N_vvr: float = -0.0498
    N_vrr: float = -0.0151
    N_rrr: float = -0.0061

    # ===== 横摇 (1-DOF pendulum) =====
    G_M: float = 1.2
    T_phi: float = 5.0

    # ===== 螺旋桨 =====
    t_P: float = 0.184
    w_P: float = 0.200
    D_P: float = 1.5
    k_0: float = 0.6
    k_1: float = -0.3
    k_2: float = -0.25

    # ===== 舵 =====
    t_R: float = 0.387
    a_H: float = 0.312
    x_H_prime: float = -0.464
    x_R_prime: float = -0.500
    gamma_R: float = 0.395
    l_R_prime: float = -0.710
    kappa: float = 0.50
    epsilon: float = 1.09
    A_R: float = 1.65
    f_alpha: float = 2.747

    # ===== 积分 =====
    dt: float = 0.02

    # ===== 初始条件 =====
    x0: float = 0.0
    y0: float = 0.0
    psi0: float = 1.5708
    u0: float = 9.26

    # ===== 地理原点 =====
    origin_lat: float = 30.5
    origin_lon: float = 122.0

    # ===== 计算属性 =====
    @property
    def mass(self) -> float:
        """排水量质量 (kg)。"""
        return self.displacement_t * 1000.0

    @property
    def I_zz(self) -> float:
        """艏摇转动惯量 (kg·m²)。

        I_zz = m * (0.25 * L)² * (1 + J_zz_prime)
        """
        return self.mass * (0.25 * self.L) ** 2 * (1.0 + self.J_zz_prime)

    @property
    def I_xx(self) -> float:
        """横摇转动惯量 (kg·m²)。

        由横摇固有周期反推: I_xx = m * g * G_M / (2π / T_phi)²
        """
        omega_phi = 2.0 * math.pi / self.T_phi
        return self.mass * 9.81 * self.G_M / (omega_phi ** 2)

    @property
    def m_x_added(self) -> float:
        """纵向附加质量 (kg)。"""
        return self.mass * self.m_x_prime

    @property
    def m_y_added(self) -> float:
        """横向附加质量 (kg)。"""
        return self.mass * self.m_y_prime

    @property
    def J_zz_added(self) -> float:
        """艏摇附加惯矩 (kg·m²)。"""
        return self.I_zz * self.J_zz_prime / (1.0 + self.J_zz_prime)

    @property
    def scale_force(self) -> float:
        """MMG 力尺度因子: 0.5 * rho * L * d。"""
        return 0.5 * self.rho * self.L * self.d

    @property
    def scale_moment(self) -> float:
        """MMG 力矩尺度因子: 0.5 * rho * L² * d。"""
        return 0.5 * self.rho * self.L * self.L * self.d
