"""FCBPlugin — ShipMotionSimulator 实现，封装 MMGModel。

FCB (Fast Craft/Boat) 45m 半滑行船型，Yasukawa & Yoshimura 2015 4-DOF MMG。
Phase 1: load_params + step 完整代理至 MMGModel；export_fmu_interface()
返回 22 变量 FmuDescriptor（D1.3.3 FMI bridge 消费入口）。
"""

from __future__ import annotations

import yaml

from .ship_motion_simulator import (
    ShipMotionSimulator,
    FmuDescriptor,
    FmuVariableSpec,
)
from .mmg_model import ShipState, MMGModel
from .mmg_coefficients import MMGCoefficients


class FCBPlugin(ShipMotionSimulator):
    """FCB 船型动力学子系统 — YAML 参数化 + MMGModel 集成。

    Attributes:
        _model: MMGModel 实例（load_params 后创建）
        _coeffs: MMGCoefficients 实例（从 YAML 解析）
    """

    def __init__(self):
        self._model: MMGModel | None = None
        self._coeffs: MMGCoefficients | None = None

    # ── ShipMotionSimulator ABC 接口 ──────────────────────

    def load_params(self, yaml_path: str) -> None:
        """读取 YAML，构造 MMGCoefficients → MMGModel。"""
        with open(yaml_path) as f:
            raw = yaml.safe_load(f)

        # 导航到 ros__parameters 子字典
        # YAML 结构: {top_key: {ros__parameters: {param_key: val, ...}}}
        params: dict | None = None
        for top_val in raw.values():
            if isinstance(top_val, dict) and "ros__parameters" in top_val:
                params = top_val["ros__parameters"]
                break

        if params is None:
            raise ValueError(
                f"Cannot locate ros__parameters in {yaml_path}"
            )

        # 过滤只保留 MMGCoefficients 已知字段
        known = set(MMGCoefficients.__dataclass_fields__.keys())
        filtered = {k: v for k, v in params.items() if k in known}

        self._coeffs = MMGCoefficients(**filtered)
        self._model = MMGModel(self._coeffs)

    def step(
        self,
        state: ShipState,
        delta_rad: float,
        n_rps: float,
        dt_s: float,
        wind_speed: float = 0.0,
        wind_dir_rad: float = 0.0,
        current_speed: float = 0.0,
        current_dir_rad: float = 0.0,
    ) -> ShipState:
        """单步积分 → 代理至 MMGModel.rk4_step。"""
        if self._model is None:
            raise RuntimeError("FCBPlugin: load_params() must be called before step()")
        return self._model.rk4_step(
            state,
            delta_rad,
            n_rps,
            wind_speed,
            wind_dir_rad,
            current_speed,
            current_dir_rad,
        )

    def vessel_class(self) -> str:
        return "FCB"

    def hull_class(self) -> str:
        return "SEMI_PLANING"

    def export_fmu_interface(self) -> FmuDescriptor:
        """导出 FMI 2.0 模型描述（22 变量）。

        Returns:
            FmuDescriptor 含 11 output + 6 input + 5 parameter。
        """
        return FmuDescriptor(
            model_name="FCBShipDynamics",
            model_identifier="FCBShipDynamics",
            default_step_size=0.02,
            variables=[
                # ── 输出 (11) ──────────────────────────────────
                FmuVariableSpec(
                    name="u", causality="output",
                    variability="continuous", type="Real",
                    unit="m/s", description="Surge velocity",
                ),
                FmuVariableSpec(
                    name="v", causality="output",
                    variability="continuous", type="Real",
                    unit="m/s", description="Sway velocity",
                ),
                FmuVariableSpec(
                    name="r", causality="output",
                    variability="continuous", type="Real",
                    unit="rad/s", description="Yaw rate",
                ),
                FmuVariableSpec(
                    name="x", causality="output",
                    variability="continuous", type="Real",
                    unit="m", description="Position x NED",
                ),
                FmuVariableSpec(
                    name="y", causality="output",
                    variability="continuous", type="Real",
                    unit="m", description="Position y NED",
                ),
                FmuVariableSpec(
                    name="psi", causality="output",
                    variability="continuous", type="Real",
                    unit="rad", description="Heading",
                ),
                FmuVariableSpec(
                    name="phi", causality="output",
                    variability="continuous", type="Real",
                    unit="rad", description="Roll angle",
                ),
                FmuVariableSpec(
                    name="p", causality="output",
                    variability="continuous", type="Real",
                    unit="rad/s", description="Roll rate",
                ),
                FmuVariableSpec(
                    name="delta", causality="output",
                    variability="continuous", type="Real",
                    unit="rad", description="Actual rudder angle",
                ),
                FmuVariableSpec(
                    name="n", causality="output",
                    variability="continuous", type="Real",
                    unit="1/s", description="Actual propeller RPS",
                ),
                FmuVariableSpec(
                    name="sog", causality="output",
                    variability="continuous", type="Real",
                    unit="m/s", description="Speed over ground",
                ),
                # ── 输入 (6) ───────────────────────────────────
                FmuVariableSpec(
                    name="delta_cmd", causality="input",
                    variability="continuous", type="Real",
                    unit="rad",
                    description="Commanded rudder angle",
                ),
                FmuVariableSpec(
                    name="n_rps_cmd", causality="input",
                    variability="continuous", type="Real",
                    unit="1/s",
                    description="Commanded propeller RPS",
                ),
                FmuVariableSpec(
                    name="wind_dir_deg", causality="input",
                    variability="continuous", type="Real",
                    unit="deg", description="Wind direction",
                ),
                FmuVariableSpec(
                    name="wind_speed_mps", causality="input",
                    variability="continuous", type="Real",
                    unit="m/s", description="Wind speed",
                ),
                FmuVariableSpec(
                    name="current_dir_deg", causality="input",
                    variability="continuous", type="Real",
                    unit="deg",
                    description="Current direction",
                ),
                FmuVariableSpec(
                    name="current_speed_mps", causality="input",
                    variability="continuous", type="Real",
                    unit="m/s", description="Current speed",
                ),
                # ── 参数 (5) ───────────────────────────────────
                FmuVariableSpec(
                    name="L", causality="parameter",
                    variability="fixed", type="Real",
                    unit="m", description="Ship length",
                ),
                FmuVariableSpec(
                    name="B", causality="parameter",
                    variability="fixed", type="Real",
                    unit="m", description="Ship beam",
                ),
                FmuVariableSpec(
                    name="d", causality="parameter",
                    variability="fixed", type="Real",
                    unit="m", description="Ship draft",
                ),
                FmuVariableSpec(
                    name="m", causality="parameter",
                    variability="fixed", type="Real",
                    unit="kg", description="Ship mass",
                ),
                FmuVariableSpec(
                    name="Izz", causality="parameter",
                    variability="fixed", type="Real",
                    unit="kg*m2",
                    description="Yaw moment of inertia",
                ),
            ],
        )
