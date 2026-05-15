"""Yasukawa & Yoshimura 2015 4-DOF MMG 模型 — RK4 积分器。

参考文献:
  Yasukawa, H. & Yoshimura, Y., "Introduction of MMG standard method
  for ship maneuvering predictions", J. Mar. Sci. Tech., 2015 [R7]

运动方程 (船体固定坐标系, CG 原点, x_G=0):
  m11*u_dot - m22*v*r = X_total
  m22*v_dot + m11*u*r = Y_total
  m33*r_dot           = N_total
  m44*p_dot           = K_total

其中:
  m11 = mass*(1+m_x'), m22 = mass*(1+m_y'), m33 = I_zz, m44 = I_xx
"""

import math
from dataclasses import dataclass
from .mmg_coefficients import MMGCoefficients


@dataclass
class ShipState:
    """8 自由度状态向量: [x, y, psi, phi, u, v, r, p]"""

    x: float = 0.0
    y: float = 0.0
    psi: float = 0.0
    phi: float = 0.0
    u: float = 0.0
    v: float = 0.0
    r: float = 0.0
    p: float = 0.0

    def __add__(self, other: "ShipState") -> "ShipState":
        return ShipState(
            x=self.x + other.x,
            y=self.y + other.y,
            psi=self.psi + other.psi,
            phi=self.phi + other.phi,
            u=self.u + other.u,
            v=self.v + other.v,
            r=self.r + other.r,
            p=self.p + other.p,
        )

    def __mul__(self, scalar: float) -> "ShipState":
        return ShipState(
            x=self.x * scalar,
            y=self.y * scalar,
            psi=self.psi * scalar,
            phi=self.phi * scalar,
            u=self.u * scalar,
            v=self.v * scalar,
            r=self.r * scalar,
            p=self.p * scalar,
        )

    __rmul__ = __mul__


class MMGModel:
    """Yasukawa 2015 4-DOF MMG 动力学模型 + RK4 积分器。

    状态:  [x, y, psi, phi, u, v, r, p]
    控制:  [delta_cmd (rad), n_rps_cmd (rev/s)]
    环境:  [wind_speed, wind_dir, current_speed, current_dir]
    """

    def __init__(self, coeffs: MMGCoefficients):
        self.c = coeffs
        self._precompute_mass_matrix()

    def _precompute_mass_matrix(self):
        c = self.c
        self.m11 = c.mass * (1.0 + c.m_x_prime)
        self.m22 = c.mass * (1.0 + c.m_y_prime)
        self.m33 = c.I_zz
        self.m44 = c.I_xx

    # ─── 主接口 ───────────────────────────────────────────────

    def rk4_step(
        self,
        state: ShipState,
        delta_cmd: float,
        n_rps_cmd: float,
        wind_speed: float = 0.0,
        wind_dir_rad: float = 0.0,
        current_speed: float = 0.0,
        current_dir_rad: float = 0.0,
    ) -> ShipState:
        """4 阶 Runge-Kutta 推进一个时间步 (dt)。"""
        dt = self.c.dt

        k1 = self.compute_derivatives(
            state, delta_cmd, n_rps_cmd,
            wind_speed, wind_dir_rad, current_speed, current_dir_rad,
        )
        k2 = self.compute_derivatives(
            state + k1 * (0.5 * dt),
            delta_cmd, n_rps_cmd,
            wind_speed, wind_dir_rad, current_speed, current_dir_rad,
        )
        k3 = self.compute_derivatives(
            state + k2 * (0.5 * dt),
            delta_cmd, n_rps_cmd,
            wind_speed, wind_dir_rad, current_speed, current_dir_rad,
        )
        k4 = self.compute_derivatives(
            state + k3 * dt,
            delta_cmd, n_rps_cmd,
            wind_speed, wind_dir_rad, current_speed, current_dir_rad,
        )

        return state + (k1 + k2 * 2.0 + k3 * 2.0 + k4) * (dt / 6.0)

    def compute_derivatives(
        self,
        state: ShipState,
        delta_cmd: float,
        n_rps_cmd: float,
        wind_speed: float = 0.0,
        wind_dir_rad: float = 0.0,
        current_speed: float = 0.0,
        current_dir_rad: float = 0.0,
    ) -> ShipState:
        """计算完整的状态导数 d(state)/dt。"""
        c = self.c

        # ── (a) 运动学 ──
        x_dot = state.u * math.cos(state.psi) - state.v * math.sin(state.psi)
        y_dot = state.u * math.sin(state.psi) + state.v * math.cos(state.psi)
        psi_dot = state.r
        phi_dot = state.p

        # ── (b) 无因次化 ──
        U = math.sqrt(state.u ** 2 + state.v ** 2)
        if U < 1e-6:
            U = 1e-6
        v_prime = state.v / U
        r_prime = state.r * c.L / U

        # ── (c) 裸船体力 ──
        X_hull_nd = (
            c.X_vv * v_prime ** 2
            + c.X_vr * v_prime * r_prime
            + c.X_rr * r_prime ** 2
            + c.X_vvvv * v_prime ** 4
        )
        Y_hull_nd = (
            c.Y_v * v_prime
            + c.Y_r * r_prime
            + c.Y_vvv * v_prime ** 3
            + c.Y_vvr * v_prime ** 2 * r_prime
            + c.Y_vrr * v_prime * r_prime ** 2
            + c.Y_rrr * r_prime ** 3
        )
        N_hull_nd = (
            c.N_v * v_prime
            + c.N_r * r_prime
            + c.N_vvv * v_prime ** 3
            + c.N_vvr * v_prime ** 2 * r_prime
            + c.N_vrr * v_prime * r_prime ** 2
            + c.N_rrr * r_prime ** 3
        )
        U2 = U * U
        X_hull = X_hull_nd * c.scale_force * U2
        Y_hull = Y_hull_nd * c.scale_force * U2
        N_hull = N_hull_nd * c.scale_moment * U2

        # ── (d) 螺旋桨推力 ──
        X_prop = 0.0
        if n_rps_cmd >= 0.01:
            J = state.u * (1.0 - c.w_P) / (n_rps_cmd * c.D_P)
            K_T = c.k_2 * J * J + c.k_1 * J + c.k_0
            X_prop = (1.0 - c.t_P) * c.rho * n_rps_cmd ** 2 * c.D_P ** 4 * K_T

        # ── (e) 舵力 ──
        u_R = c.epsilon * state.u * (1.0 - c.w_P)
        v_R = c.gamma_R * (state.v + c.l_R_prime * c.L * state.r)
        U_R = math.sqrt(u_R ** 2 + v_R ** 2)
        if U_R < 1e-6:
            U_R = 1e-6
        alpha_R = delta_cmd - math.atan2(v_R, u_R)
        F_N = 0.5 * c.rho * c.A_R * c.f_alpha * U_R * U_R * math.sin(alpha_R)
        X_R = -(1.0 - c.t_R) * F_N * math.sin(delta_cmd)
        Y_R = -(1.0 + c.a_H) * F_N * math.cos(delta_cmd)
        N_R = -((c.x_R_prime + c.a_H * c.x_H_prime) * c.L) * F_N * math.cos(delta_cmd)

        # ── (f) 环境力 (二次阻力简化) ──
        X_env = 0.0
        Y_env = 0.0
        rho_air = 1.225
        wind_area = c.B * (c.L * 0.3)  # 侧投影面积近似
        wind_drag_coeff = 0.8

        if wind_speed > 0.01:
            F_wind = 0.5 * rho_air * wind_area * wind_drag_coeff * wind_speed ** 2
            X_env += F_wind * math.cos(wind_dir_rad - state.psi)
            Y_env += F_wind * math.sin(wind_dir_rad - state.psi)

        if current_speed > 0.01:
            F_current = 0.5 * c.rho * c.L * c.d * 0.05 * current_speed ** 2
            X_env += F_current * math.cos(current_dir_rad - state.psi)
            Y_env += F_current * math.sin(current_dir_rad - state.psi)

        # ── (g & h) 质量矩阵求解 ──
        X_total = X_hull + X_prop + X_R + X_env
        Y_total = Y_hull + Y_R + Y_env
        N_total = N_hull + N_R

        u_dot = (X_total + self.m22 * state.v * state.r) / self.m11
        v_dot = (Y_total - self.m11 * state.u * state.r) / self.m22
        r_dot = N_total / self.m33

        # ── (i) 横摇 (线性 1-DOF) ──
        K_phi = c.mass * 9.81 * c.G_M
        K_p = K_phi * c.T_phi / (2.0 * math.pi) * 0.1
        p_dot = (-K_phi * state.phi - K_p * state.p) / self.m44

        return ShipState(
            x=x_dot, y=y_dot, psi=psi_dot, phi=phi_dot,
            u=u_dot, v=v_dot, r=r_dot, p=p_dot,
        )
