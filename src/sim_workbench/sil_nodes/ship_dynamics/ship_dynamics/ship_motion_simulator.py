"""ShipMotionSimulator 抽象基类 — pluginlib 风格的 Python plugin 契约。

对应 C++ ship_sim_interfaces::ShipMotionSimulator，Python 实现使用 abc.ABC。
D1.3.3 FMI bridge 消费 export_fmu_interface() 返回值生成 modelDescription.xml。
"""

from __future__ import annotations
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from .mmg_model import ShipState  # 复用既有 ShipState


@dataclass
class FmuVariableSpec:
    """单个 FMI 2.0 变量描述 — 对应 FMI modelDescription.xml <ScalarVariable>。"""

    name: str
    causality: str  # "input" | "output" | "parameter" | "local"
    variability: str  # "continuous" | "discrete" | "fixed" | "tunable"
    type: str  # "Real" | "Integer" | "Boolean" | "String"
    unit: str  # "m/s", "rad", "rad/s", "1" (dimensionless)
    start: float = 0.0  # 初始值 (Real 类型)
    description: str = ""  # ≤120 chars


@dataclass
class FmuDescriptor:
    """FMI 2.0 模型描述容器 — export_fmu_interface() 返回值。

    D1.3.3 消费路径:
        1. 遍历 variables[] → 生成 modelDescription.xml
        2. causality=="input" 变量 ↔ ROS2 subscriber topic
        3. causality=="output" 变量 ↔ ROS2 publisher topic
    """

    fmi_version: str = "2.0"
    model_name: str = ""
    model_identifier: str = ""
    default_step_size: float = 0.02
    variables: list[FmuVariableSpec] = field(default_factory=list)


class ShipMotionSimulator(ABC):
    """船舶运动仿真器抽象基类。

    所有 vessel plugin 必须实现此接口。ShipDynamicsNode 通过
    YAML vessel_class 字段动态加载具体 plugin，不硬编码船型。
    """

    @abstractmethod
    def load_params(self, yaml_path: str) -> None:
        """从 YAML 文件加载船型特定参数 (Capability Manifest)。"""

    @abstractmethod
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
        """单步积分: 输入当前状态 + 控制/环境量，输出 dt_s 后的新状态。"""

    @abstractmethod
    def vessel_class(self) -> str:
        """返回 vessel class 标识符 (如 "FCB")。"""

    @abstractmethod
    def hull_class(self) -> str:
        """返回 hull class 标识符 (如 "SEMI_PLANING")。"""

    @abstractmethod
    def export_fmu_interface(self) -> FmuDescriptor:
        """导出 FMI 2.0 模型描述 (D1.3.3 消费)。

        Phase 1 仅签名 + FCBPlugin stub 实装。D1.3.3 消费
        FmuDescriptor 生成 modelDescription.xml + dds-fmu mapping。
        新 vessel plugin 只需填变量清单，无业务逻辑。
        """
