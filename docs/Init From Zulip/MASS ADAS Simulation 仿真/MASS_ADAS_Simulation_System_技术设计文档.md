# MASS_ADAS Simulation System 仿真系统技术设计文档

**文档编号：** SANGO-ADAS-SIM-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  
**状态：** 开发参考

---

## 目录

1. 概述与设计目标
2. 仿真系统总体架构
3. 仿真保真度分级
4. 船舶动力学仿真引擎
5. 环境仿真引擎
6. 传感器仿真引擎
7. 交通仿真引擎
8. 海图与水域仿真
9. 通信仿真
10. SIL/HIL 集成架构
11. 场景管理系统
12. 可视化与回放系统
13. 自动化测试框架
14. 评估指标与评分系统
15. 多船型参数化配置
16. 极端场景与对抗测试
17. 分布式仿真与联合仿真
18. 仿真数据管理
19. 仿真与真实系统的接口协议
20. 开发路线图与工具链选型
21. 内部子系统分解
22. 参数总览表
23. 测试策略与验收标准
24. 参考文献与标准

---

## 1. 概述与设计目标

### 1.1 为什么需要仿真系统

MASS_ADAS 从代码开发到实船部署之间有一个巨大的鸿沟——海上测试极其昂贵（每天的船运营成本、保险、人员），而且危险场景（如碰撞风险、设备故障）无法在真实环境中安全复现。仿真系统是跨越这个鸿沟的桥梁。

具体来说，仿真系统需要支持：

**开发阶段：** 开发人员在桌面上调试单个模块——比如 COLREGs Engine 开发者需要快速生成各种会遇场景来验证分类逻辑。

**集成阶段：** 多个模块联调——比如从 Perception 到 L3 到 L4 到 L5 的完整信号链，验证端到端的避碰决策是否正确执行。

**验证阶段：** 系统性地运行数百个测试场景，验证系统在各种工况下的表现——正常航行、交叉会遇、多目标避碰、恶劣天气、设备故障。

**认证阶段：** 向船级社和海事主管机关证明系统的安全性——仿真结果是安全论证（Safety Case）的重要证据之一。

**训练阶段：** 为 AI/ML 模型生成训练数据——合成的雷达图像、摄像头视图、会遇场景，补充真实数据的不足。

### 1.2 设计目标

| 目标 | 量化指标 | 说明 |
|------|---------|------|
| 多船型支持 | ≥ 5 种船型参数配置 | 12m USV、40m 引航船、80m 渡船、200m 货船、小型渔船 |
| 场景多样性 | ≥ 200 个预定义场景 | 覆盖 COLREGs 全部会遇类型 + 水域类型 + 天气条件 |
| 动力学保真度 | 与实船偏差 < 15% | 旋回、停船、横摇等关键动态特性 |
| 传感器仿真真实感 | AI 模型可迁移 | 仿真训练的模型在真实传感器上仍然有效 |
| 实时性 | ≥ 10× 实时 | 正常模式实时运行，加速模式 10 倍速 |
| 可扩展性 | 100+ 目标同时仿真 | 密集交通场景 |
| SIL 透明接入 | ROS2 话题完全兼容 | MASS_ADAS 的 ROS2 节点无需任何修改即可在仿真中运行 |

### 1.3 核心设计理念——"传感器级仿真"

仿真系统的设计理念是**在传感器输出层面注入仿真数据**，而非在模块内部做桩（stub）。

```
真实系统：
  真实传感器 → Data Preprocessing → Object Detection → ... → L5

仿真系统：
  仿真传感器 → Data Preprocessing → Object Detection → ... → L5
       ↑
  仿真引擎生成的虚拟传感器数据
  （雷达回波、AIS 消息、摄像头图像、GPS NMEA）
```

这意味着 Data Preprocessing 以下的全部 MASS_ADAS 模块在仿真中运行的代码与在真实系统中完全相同——没有任何模拟替代品。这保证了仿真验证的有效性——仿真中验证通过的逻辑在真实系统中也应该工作。

---

## 2. 仿真系统总体架构

### 2.1 架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                    Simulation Manager                           │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐        │
│  │ Scenario │  │  Clock   │  │ Metrics  │  │Visualizer│        │
│  │ Manager  │  │ Manager  │  │ Recorder │  │ & Replay │        │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘        │
└─────────────┬───────────────────────────────────────────────────┘
              │ 场景参数 + 时钟控制
              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Simulation Core                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │  Ship        │  │ Environment  │  │  Traffic     │          │
│  │  Dynamics    │  │   Engine     │  │  Generator   │          │
│  │  Engine      │  │              │  │              │          │
│  │ (own ship)   │  │ wind/wave/   │  │ target ships │          │
│  │              │  │ current/vis  │  │ (COLREGs AI) │          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │
│         │                 │                  │                  │
│  ┌──────┴─────────────────┴──────────────────┴───────┐          │
│  │              Sensor Simulator                      │          │
│  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐    │          │
│  │  │Radar │ │ AIS  │ │Camera│ │LiDAR │ │ Nav  │    │          │
│  │  │ Sim  │ │ Sim  │ │ Sim  │ │ Sim  │ │ Sim  │    │          │
│  │  └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘ └──┬───┘    │          │
│  └─────┼────────┼────────┼────────┼────────┼────────┘          │
└────────┼────────┼────────┼────────┼────────┼────────────────────┘
         │        │        │        │        │
    ┌────┴────────┴────────┴────────┴────────┴─────────────────┐
    │          ROS2 Middleware (DDS)                             │
    │   仿真传感器数据 ←→ MASS_ADAS 模块（不修改的生产代码）      │
    └──────────────────────────────────────────────────────────┘
         │
    ┌────┴─────────────────────────────────────────────────────┐
    │                MASS_ADAS Production Software              │
    │   Data Preprocessing → Perception → L3 → L4 → L5         │
    │   （与实船运行完全相同的代码）                               │
    └──────────────────────────────────────────────────────────┘
         │
         ▼ 控制指令（δ_cmd, RPM_cmd）
    ┌──────────────────────────────────────────────────────────┐
    │   Ship Dynamics Engine（接收控制指令，更新船舶状态）         │
    │   → 新的位置/航向/速度 → 传感器仿真器 → 下一周期            │
    └──────────────────────────────────────────────────────────┘
```

### 2.2 闭环仿真

仿真系统是闭环的——Ship Dynamics Engine 根据 L5 输出的控制指令（舵角、转速）更新船舶状态（位置、航向、速度），新的状态又通过传感器仿真器反馈回 MASS_ADAS 的感知模块。这形成了与真实系统完全一致的控制回路。

```
时钟步进 → Ship Dynamics 更新 → Environment 更新 → Sensor 生成 → MASS_ADAS 处理 → 控制指令 → 时钟步进
```

---

## 3. 仿真保真度分级

不同的使用场景需要不同级别的仿真保真度。高保真度意味着高计算成本，因此按需选择。

| 级别 | 名称 | 动力学 | 环境 | 传感器 | 交通 | 适用场景 | 速度 |
|------|------|-------|------|--------|------|---------|------|
| L1 | Kinematic | 运动学（无惯性） | 无 | 理想化 | 直线匀速 | 算法单元测试 | 100× |
| L2 | Basic Dynamics | 3DOF Nomoto | 恒定风流 | 加高斯噪声 | 简单行为 | 模块集成测试 | 50× |
| L3 | Full Dynamics | 3DOF+非线性 | 风+流+波浪（谱模型） | 雷达杂波+AIS延迟 | COLREGs行为 | 系统验证 | 10× |
| L4 | High Fidelity | 6DOF+横摇+浅水 | 真实气象数据 | 雷达PPI图像+摄像头渲染 | 智能交通 | 安全认证 | 1~2× |
| L5 | Photorealistic | 同L4 | 同L4 | 照片级摄像头+真实雷达回波 | 同L4 | AI训练数据生成 | 0.5~1× |

**建议：** V1 开发使用 L2/L3，V2 验证使用 L3/L4，AI 训练使用 L5。

---

## 4. 船舶动力学仿真引擎

### 4.1 3DOF 船舶运动模型

对于水面船舶，核心运动自由度是：纵荡（surge）、横荡（sway）、艏摇（yaw）。

```
状态向量：η = [x, y, ψ]ᵀ（位置和航向，地球坐标系）
             ν = [u, v, r]ᵀ（速度和角速度，船体坐标系）

运动学方程：
  dx/dt = u cos(ψ) - v sin(ψ)
  dy/dt = u sin(ψ) + v cos(ψ)
  dψ/dt = r

动力学方程（Fossen 标准形式）：
  M dν/dt + C(ν)ν + D(ν)ν = τ + τ_env

  M = 惯性矩阵（含附加质量）
  C(ν) = 科氏力和向心力矩阵
  D(ν) = 阻尼矩阵（线性+非线性）
  τ = 推进力/力矩向量（来自舵、螺旋桨、侧推器）
  τ_env = 环境力/力矩（风、流、波浪）
```

### 4.2 参数化船型模型

为了支持多船型，动力学参数通过配置文件加载：

```yaml
# ship_config_usv_12m.yaml — SANGO 12m USV
ship_type: "USV_12m"
hull:
  length: 12.0              # m
  beam: 3.5                 # m
  draft: 0.8                # m
  displacement: 8000        # kg
  block_coefficient: 0.48   # Cb

inertia:
  m: 8000                   # kg（质量）
  Iz: 28800                 # kg·m²（艏摇转动惯量，≈ m × L² / 12 × 1.2）
  x_g: 0.5                 # m（重心在中纵距前方的偏移）

added_mass:
  X_udot: -400              # kg（纵荡附加质量，≈ 5% m）
  Y_vdot: -5600             # kg（横荡附加质量，≈ 70% m）
  N_rdot: -7200             # kg·m²（艏摇附加惯量，≈ 25% Iz）

damping:
  # 线性阻尼
  X_u: -200                 # N/(m/s)
  Y_v: -1500                # N/(m/s)
  N_r: -2000                # N·m/(rad/s)
  # 非线性阻尼（|v|v 形式）
  X_uu: -300                # N/(m/s)²
  Y_vv: -3000               # N/(m/s)²
  N_rr: -3500               # N·m/(rad/s)²

propulsion:
  # 螺旋桨
  propeller_type: "fixed_pitch"
  max_rpm: 2000
  thrust_coefficient: 0.35   # Kt（推力系数）
  propeller_diameter: 0.4    # m
  wake_fraction: 0.15        # w（伴流分数）
  thrust_deduction: 0.10     # t（推力减额分数）
  
  # 舵
  rudder_area: 0.15          # m²
  rudder_max_angle: 35       # 度
  rudder_rate_max: 5.0       # 度/秒
  rudder_coefficient: 0.8    # 舵效系数
  
  # 侧推器（可选）
  bow_thruster_max: 500      # N
  bow_thruster_position: 5.0 # m（距重心的前向距离）

turning:
  advance_at_max_speed: 48   # m（全速满舵纵距）
  transfer_at_max_speed: 24  # m（全速满舵横距）
  tactical_diameter: 60      # m（全速满舵战术直径）
  
stopping:
  crash_stop_distance: 36    # m（紧急停船距离）
  crash_stop_time: 25        # s（紧急停船时间）

environment_response:
  wind_drag_coefficient_x: 0.8   # 纵向风阻系数
  wind_drag_coefficient_y: 1.2   # 横向风阻系数
  wind_drag_area_x: 8.0          # m²（纵向受风面积）
  wind_drag_area_y: 18.0         # m²（横向受风面积）
```

### 4.3 其他船型配置示例

```yaml
# ship_config_cargo_200m.yaml — 200m 散货船
ship_type: "Bulk_Carrier_200m"
hull:
  length: 200.0
  beam: 32.0
  draft: 12.5
  displacement: 50000000    # 50000 吨
  block_coefficient: 0.82

turning:
  advance_at_max_speed: 600  # m
  tactical_diameter: 800     # m

stopping:
  crash_stop_distance: 2500  # m（约 15 倍船长！）
  crash_stop_time: 300       # s（5 分钟）
```

### 4.4 动力学引擎实现

```python
class ShipDynamicsEngine:
    
    def __init__(self, config_file):
        self.params = load_yaml(config_file)
        self.state = ShipState()  # η, ν
        self.dt = 0.01  # 100 Hz 内部仿真步长
    
    def step(self, control_input, environment):
        """
        control_input: {rudder_deg, rpm, bow_thrust_N}
        environment: {wind_speed, wind_dir, current_speed, current_dir, wave_height, wave_period}
        """
        # 1. 计算推进力
        tau_propulsion = self.compute_propulsion(control_input)
        
        # 2. 计算环境力
        tau_env = self.compute_environment_forces(environment)
        
        # 3. 动力学积分
        # M * dν/dt = tau_propulsion + tau_env - C(ν)*ν - D(ν)*ν
        nu_dot = np.linalg.solve(self.M, 
            tau_propulsion + tau_env - self.C(self.state.nu) @ self.state.nu - self.D(self.state.nu) @ self.state.nu
        )
        
        # 4. 欧拉积分（可替换为 RK4）
        self.state.nu += nu_dot * self.dt
        
        # 5. 运动学更新
        R = self.rotation_matrix(self.state.psi)
        eta_dot = R @ self.state.nu
        self.state.eta += eta_dot * self.dt
        
        # 6. 归一化航向
        self.state.psi = self.state.eta[2] % (2 * np.pi)
        
        return self.state
    
    def compute_propulsion(self, ctrl):
        """计算螺旋桨推力和舵力"""
        # 螺旋桨推力
        n = ctrl.rpm / 60  # rev/s
        T = self.params.Kt * self.params.rho * n**2 * self.params.D_prop**4
        X_prop = (1 - self.params.t_deduction) * T
        
        # 舵力（依赖流速）
        delta = ctrl.rudder_deg * np.pi / 180
        V_rudder = self.state.nu[0] * (1 - self.params.w)  # 舵上的流速
        F_rudder = 0.5 * self.params.rho * self.params.A_rudder * self.params.C_rudder * V_rudder**2 * np.sin(delta)
        
        X_rudder = -F_rudder * np.sin(delta)  # 舵的纵向分力（阻力）
        Y_rudder = -F_rudder * np.cos(delta)  # 舵的横向分力
        N_rudder = Y_rudder * self.params.x_rudder  # 舵力矩
        
        # 侧推器
        Y_bow = ctrl.bow_thrust_N
        N_bow = ctrl.bow_thrust_N * self.params.x_bow_thruster
        
        return np.array([X_prop + X_rudder, Y_rudder + Y_bow, N_rudder + N_bow])
```

### 4.5 Nomoto 简化模型（L1/L2 保真度用）

```python
class NomotoModel:
    """一阶 Nomoto 模型——最简单的航向动力学"""
    
    def __init__(self, K, T):
        self.K = K  # 旋回性增益
        self.T = T  # 追随性时间常数
        self.r = 0  # 角速度
        self.psi = 0  # 航向
    
    def step(self, delta, dt):
        # T * dr/dt + r = K * delta
        dr_dt = (self.K * delta - self.r) / self.T
        self.r += dr_dt * dt
        self.psi += self.r * dt
        return self.psi, self.r
```

### 4.6 6DOF 扩展（横摇、纵摇、垂荡）

对于 L4/L5 保真度，增加横摇（roll）、纵摇（pitch）和垂荡（heave）——这对传感器仿真（雷达方位误差受横摇影响）和控制性能评估（横摇引起的舵效降低）很重要。

```python
# 6DOF 状态向量：
# η = [x, y, z, φ, θ, ψ]  （位置 + 欧拉角）
# ν = [u, v, w, p, q, r]  （线速度 + 角速度）

# 横摇动力学（简化为单自由度受迫振动）：
# (I_xx + A_44) * dp/dt + B_44 * p + C_44 * φ = M_wave + M_wind + M_rudder
#
# 其中：
# I_xx = 横摇转动惯量
# A_44 = 附加横摇惯量
# B_44 = 横摇阻尼
# C_44 = 横摇恢复力矩 = Δ × GM × sin(φ)
# M_wave = 波浪激励力矩
```

---

## 5. 环境仿真引擎

### 5.1 风模型

```python
class WindModel:
    
    def __init__(self, mean_speed, mean_direction, gust_factor=1.3):
        self.mean_speed = mean_speed        # m/s
        self.mean_direction = mean_direction  # deg (from)
        self.gust_factor = gust_factor
        
        # Davenport 风谱参数
        self.turbulence_intensity = 0.15  # 湍流强度
    
    def get_wind(self, t, position):
        """返回 t 时刻 position 处的风速和风向"""
        
        # 阵风模型：低频正弦波动 + 高频随机湍流
        gust = self.mean_speed * self.turbulence_intensity * (
            0.5 * np.sin(2 * np.pi * t / 60) +    # 60 秒周期的大尺度波动
            0.3 * np.sin(2 * np.pi * t / 15) +    # 15 秒周期
            0.2 * np.random.randn()                 # 随机湍流
        )
        
        speed = max(0, self.mean_speed + gust)
        
        # 风向波动 ±10°
        direction = self.mean_direction + 10 * np.sin(2 * np.pi * t / 120) + 5 * np.random.randn()
        
        return WindState(speed=speed, direction=direction % 360)
```

### 5.2 波浪模型

```python
class WaveModel:
    """基于 JONSWAP 谱的波浪仿真"""
    
    def __init__(self, Hs, Tp, direction, gamma=3.3):
        self.Hs = Hs            # 有义波高 (m)
        self.Tp = Tp            # 谱峰周期 (s)
        self.direction = direction  # 主波向 (deg)
        self.gamma = gamma      # JONSWAP 峰增强因子
        
        # 生成波浪分量（叠加法）
        self.N_components = 50  # 50 个频率分量叠加
        self.components = self._generate_components()
    
    def _generate_components(self):
        """基于 JONSWAP 谱生成波浪分量"""
        omega_p = 2 * np.pi / self.Tp
        omega_range = np.linspace(0.3 * omega_p, 3.0 * omega_p, self.N_components)
        d_omega = omega_range[1] - omega_range[0]
        
        components = []
        for omega in omega_range:
            S = self._jonswap_spectrum(omega, omega_p)
            amplitude = np.sqrt(2 * S * d_omega)
            phase = np.random.uniform(0, 2 * np.pi)
            components.append((omega, amplitude, phase))
        
        return components
    
    def _jonswap_spectrum(self, omega, omega_p):
        """JONSWAP 谱密度"""
        alpha = 0.0081  # Phillips 常数
        g = 9.81
        sigma = 0.07 if omega <= omega_p else 0.09
        
        PM = (alpha * g**2 / omega**5) * np.exp(-1.25 * (omega_p / omega)**4)
        gamma_factor = self.gamma ** np.exp(-0.5 * ((omega - omega_p) / (sigma * omega_p))**2)
        
        return PM * gamma_factor
    
    def get_wave_elevation(self, x, y, t):
        """返回 (x,y) 位置 t 时刻的波面高度"""
        eta = 0
        k_dir = self.direction * np.pi / 180
        
        for omega, amp, phase in self.components:
            k = omega**2 / 9.81  # 深水色散关系
            eta += amp * np.cos(k * (x * np.cos(k_dir) + y * np.sin(k_dir)) - omega * t + phase)
        
        return eta
    
    def get_wave_forces(self, ship_params, ship_heading, t):
        """计算作用在船上的波浪力（简化）"""
        # 二阶漂移力（低频）+ 一阶波浪力（高频）
        relative_wave_dir = self.direction - ship_heading
        
        # 二阶纵向漂移力
        F_drift_x = -0.5 * 1025 * 9.81 * self.Hs**2 * ship_params.beam * np.cos(relative_wave_dir * np.pi / 180)
        
        # 二阶横向漂移力
        F_drift_y = -0.5 * 1025 * 9.81 * self.Hs**2 * ship_params.length * np.sin(relative_wave_dir * np.pi / 180)
        
        # 一阶波浪激励力矩（引起艏摇）
        M_wave = F_drift_y * ship_params.length * 0.1 * np.sin(2 * np.pi * t / self.Tp)
        
        return np.array([F_drift_x, F_drift_y, M_wave])
```

### 5.3 海流模型

```python
class CurrentModel:
    
    def __init__(self, speed, direction, spatial_variation=False):
        self.speed = speed          # m/s
        self.direction = direction  # deg (going to)
        self.spatial_variation = spatial_variation
    
    def get_current(self, x, y, t):
        """返回位置 (x,y) 时刻 t 的海流速度"""
        
        if not self.spatial_variation:
            Vc = self.speed
            Dc = self.direction
        else:
            # 简化的空间变化：近岸流速增大（潮流效应）
            distance_to_coast = self.get_coast_distance(x, y)
            if distance_to_coast < 5000:  # 5km 内
                Vc = self.speed * (1 + 0.5 * (1 - distance_to_coast / 5000))
            else:
                Vc = self.speed
            
            # 潮流的时间变化（半日潮，周期约 12.42 小时）
            tidal_factor = np.sin(2 * np.pi * t / (12.42 * 3600))
            Vc *= abs(tidal_factor)
            Dc = self.direction if tidal_factor >= 0 else (self.direction + 180) % 360
        
        Vx = Vc * np.sin(Dc * np.pi / 180)  # 东向分量
        Vy = Vc * np.cos(Dc * np.pi / 180)  # 北向分量
        
        return CurrentState(vx=Vx, vy=Vy, speed=Vc, direction=Dc)
```

### 5.4 能见度模型

```python
class VisibilityModel:
    
    def __init__(self, base_visibility_nm, fog_events=None):
        self.base_visibility = base_visibility_nm
        self.fog_events = fog_events or []  # [(start_time, end_time, min_visibility), ...]
    
    def get_visibility(self, t):
        for start, end, min_vis in self.fog_events:
            if start <= t <= end:
                # 雾的渐变模型：进入和离开时渐变
                ramp_time = (end - start) * 0.2  # 前 20% 渐入
                if t < start + ramp_time:
                    factor = (t - start) / ramp_time
                    return self.base_visibility * (1 - factor) + min_vis * factor
                elif t > end - ramp_time:
                    factor = (end - t) / ramp_time
                    return self.base_visibility * (1 - factor) + min_vis * factor
                else:
                    return min_vis
        return self.base_visibility
```

---

## 6. 传感器仿真引擎

### 6.1 雷达仿真器

```python
class RadarSimulator:
    """生成模拟的雷达 ARPA 目标或原始扫描数据"""
    
    def __init__(self, radar_config):
        self.max_range = radar_config.max_range_m
        self.beam_width = radar_config.beam_width_deg
        self.range_resolution = radar_config.range_resolution_m
        self.scan_period = radar_config.scan_period_s  # 典型 2~3 秒
        self.noise_std_range = radar_config.noise_std_range_m  # 距离噪声 ±20m
        self.noise_std_bearing = radar_config.noise_std_bearing_deg  # 方位噪声 ±1°
    
    def generate_arpa_targets(self, own_ship, targets, environment, t):
        """生成 ARPA 目标列表（TTM 格式）"""
        
        arpa_targets = []
        
        for target in targets:
            # 计算真实的距离和方位
            dx = target.x - own_ship.x
            dy = target.y - own_ship.y
            true_range = np.sqrt(dx**2 + dy**2)
            true_bearing = np.degrees(np.arctan2(dx, dy)) % 360
            
            # 超出量程不检测
            if true_range > self.max_range:
                continue
            
            # 检测概率（距离越远越低）
            P_detect = self.compute_detection_probability(true_range, target.rcs, environment)
            if np.random.random() > P_detect:
                continue  # 未检测到
            
            # 添加测量噪声
            measured_range = true_range + np.random.normal(0, self.noise_std_range)
            measured_bearing = true_bearing + np.random.normal(0, self.noise_std_bearing)
            
            # 速度和航向（从多扫描推导，有延迟和噪声）
            measured_speed = target.speed + np.random.normal(0, 0.5)  # ±0.5 m/s
            measured_course = target.course + np.random.normal(0, 3)   # ±3°
            
            arpa_targets.append(ArpaTarget(
                range_m=measured_range,
                bearing_deg=measured_bearing % 360,
                speed_mps=max(0, measured_speed),
                course_deg=measured_course % 360,
                rcs_dbsm=target.rcs
            ))
        
        return arpa_targets
    
    def compute_detection_probability(self, range_m, rcs_dbsm, environment):
        """基于雷达方程的检测概率"""
        # 简化：P_detect ≈ 1 - (range / max_range)^4 × 10^(-rcs/20)
        range_factor = (range_m / self.max_range) ** 4
        rcs_factor = 10 ** (-rcs_dbsm / 20)
        
        # 降水衰减
        rain_attenuation = 1.0
        if environment.precipitation == "moderate_rain":
            rain_attenuation = 0.8
        elif environment.precipitation == "heavy_rain":
            rain_attenuation = 0.5
        
        P = max(0, min(1, (1 - range_factor * rcs_factor) * rain_attenuation))
        return P
    
    def generate_sea_clutter(self, own_ship, environment):
        """生成海杂波"""
        clutter_targets = []
        sea_state = environment.sea_state
        
        # 海杂波范围随海况增大
        clutter_range = {0: 0, 1: 500, 2: 1000, 3: 2000, 4: 3000, 5: 5000}[min(sea_state, 5)]
        
        n_clutter = int(sea_state * 5)  # 杂波数量
        for _ in range(n_clutter):
            r = np.random.uniform(30, clutter_range)
            b = np.random.uniform(0, 360)
            clutter_targets.append(ArpaTarget(
                range_m=r, bearing_deg=b, speed_mps=0, course_deg=0, rcs_dbsm=-10,
                is_clutter=True
            ))
        
        return clutter_targets
```

### 6.2 AIS 仿真器

```python
class AisSimulator:
    """生成模拟的 AIS NMEA 消息"""
    
    def __init__(self):
        self.report_timers = {}  # MMSI → 上次报告时间
    
    def generate_ais_messages(self, targets, t):
        """按 IMO 规定的报告间隔生成 AIS 消息"""
        messages = []
        
        for target in targets:
            if not target.has_ais:
                continue
            
            # 计算报告间隔
            interval = self.get_report_interval(target.nav_status, target.speed_knots, target.turning)
            
            last_report = self.report_timers.get(target.mmsi, 0)
            if t - last_report < interval:
                continue
            
            self.report_timers[target.mmsi] = t
            
            # 生成 NMEA !AIVDM 消息
            # 位置添加 GPS 噪声
            lat = target.latitude + np.random.normal(0, 0.00005)   # ≈5m 噪声
            lon = target.longitude + np.random.normal(0, 0.00005)
            sog = target.speed_knots + np.random.normal(0, 0.2)    # ±0.2 节
            cog = target.course + np.random.normal(0, 0.5)          # ±0.5°
            heading = target.heading if target.heading is not None else 511  # 511=不可用
            
            msg = AisMessage(
                msg_type=1,  # 位置报告
                mmsi=target.mmsi,
                nav_status=target.nav_status,
                latitude=lat,
                longitude=lon,
                sog=max(0, sog),
                cog=cog % 360,
                heading=heading,
                rot=target.rot,
                timestamp=int(t) % 60
            )
            
            # 编码为 NMEA !AIVDM 语句
            nmea_sentence = encode_ais_to_nmea(msg)
            messages.append(nmea_sentence)
        
        return messages
    
    def get_report_interval(self, nav_status, sog, turning):
        if nav_status in [1, 5, 6]:  # 锚泊/系泊
            return 180  # 3 分钟
        if turning:
            if sog < 14: return 3.33  # 每 3.33 秒
            return 2
        if sog < 3: return 10
        if sog < 14: return 6
        if sog < 23: return 6
        return 2
```

### 6.3 摄像头仿真器

```python
class CameraSimulator:
    """生成模拟的摄像头图像——两种模式"""
    
    def __init__(self, camera_config, mode="bbox"):
        self.config = camera_config
        self.mode = mode  # "bbox" = 简化（只返回检测框）, "render" = 渲染图像
    
    def generate_frame(self, own_ship, targets, environment, t):
        
        if self.mode == "bbox":
            return self.generate_bbox_detections(own_ship, targets, environment)
        else:
            return self.render_scene(own_ship, targets, environment, t)
    
    def generate_bbox_detections(self, own_ship, targets, environment):
        """简化模式：直接生成检测框（跳过图像渲染和 AI 推理）"""
        detections = []
        
        for target in targets:
            bearing = compute_bearing(own_ship, target)
            relative_bearing = (bearing - own_ship.heading) % 360
            
            # 检查是否在视场角内
            if not self.in_fov(relative_bearing):
                continue
            
            distance = compute_distance(own_ship, target)
            
            # 能见度限制
            if distance / 1852 > environment.visibility_nm:
                continue
            
            # 检测概率（距离越远越低）
            P_detect = min(1.0, 3000 / max(distance, 100))  # 3km 内接近 100%
            if np.random.random() > P_detect:
                continue
            
            # 生成像素坐标的检测框
            pixel_x = self.bearing_to_pixel(relative_bearing)
            pixel_width = self.estimate_pixel_width(target.length, distance)
            
            detections.append(CameraDetection(
                bbox=[pixel_x - pixel_width/2, 300, pixel_x + pixel_width/2, 400],
                classification=target.ship_type,
                confidence=P_detect * 0.9,
                bearing_deg=bearing
            ))
        
        return detections
    
    def render_scene(self, own_ship, targets, environment, t):
        """高保真模式：使用 3D 渲染引擎生成照片级图像"""
        # 使用 Unity/Unreal Engine 或 Open3D 渲染
        # 包含：海面、天空、船舶 3D 模型、灯光、雾效
        # 这是 L5 保真度仿真的核心组件
        raise NotImplementedError("需要集成 3D 渲染引擎")
```

### 6.4 GPS/导航传感器仿真器

```python
class NavSensorSimulator:
    
    def __init__(self):
        self.gps_noise_std = 2.0      # m（标准 GPS）
        self.compass_noise_std = 0.5  # 度
        self.speed_log_noise_std = 0.1  # m/s
    
    def generate_gps(self, true_position, t):
        """生成模拟 GPS NMEA 语句"""
        lat = true_position.lat + np.random.normal(0, self.gps_noise_std / 111111)
        lon = true_position.lon + np.random.normal(0, self.gps_noise_std / (111111 * np.cos(np.radians(true_position.lat))))
        
        hdop = 1.0 + abs(np.random.normal(0, 0.3))
        num_sats = int(np.clip(np.random.normal(10, 2), 4, 14))
        
        gga = f"$GPGGA,{format_utc(t)},{format_lat(lat)},{format_lon(lon)},1,{num_sats:02d},{hdop:.1f},{true_position.alt:.1f},M,,,,*"
        gga += compute_nmea_checksum(gga)
        
        return gga
    
    def generate_compass(self, true_heading, t):
        heading = true_heading + np.random.normal(0, self.compass_noise_std)
        hdt = f"$HEHDT,{heading:.1f},T*"
        hdt += compute_nmea_checksum(hdt)
        return hdt
```

---

## 7. 交通仿真引擎

### 7.1 目标船行为模型

交通仿真引擎生成虚拟的目标船舶，每艘都有自己的航行行为。行为模型从简单到复杂分为几个层次：

```python
class TargetShipBehavior:
    """目标船行为基类"""
    pass

class StraightLineBehavior(TargetShipBehavior):
    """L1：直线匀速——最简单，用于单元测试"""
    def step(self, dt):
        self.x += self.speed * np.sin(np.radians(self.course)) * dt
        self.y += self.speed * np.cos(np.radians(self.course)) * dt

class WaypointFollowingBehavior(TargetShipBehavior):
    """L2：航点跟踪——目标船沿预定义航路点航行"""
    def step(self, dt):
        # 简化的 LOS 引导
        target_wp = self.waypoints[self.current_wp]
        desired_heading = compute_bearing(self.position, target_wp)
        heading_error = normalize_pm180(desired_heading - self.heading)
        self.heading += np.clip(heading_error, -2, 2) * dt  # 最大 2°/s
        self.update_position(dt)

class ColregsCompliantBehavior(TargetShipBehavior):
    """L3：COLREGs 合规行为——目标船会按规则避让"""
    
    def step(self, dt, other_ships):
        # 1. 检查是否需要避让
        for other in other_ships:
            cpa, tcpa = compute_cpa_tcpa(self, other)
            if cpa < self.cpa_threshold and tcpa > 0:
                encounter = classify_encounter(self, other)
                if encounter.my_role == "give_way":
                    self.execute_avoidance(other, encounter)
        
        # 2. 正常航行
        self.follow_waypoints(dt)
    
    def execute_avoidance(self, target, encounter):
        if encounter.type == "head_on":
            self.heading += 30  # 向右转 30°
        elif encounter.type == "crossing" and encounter.my_role == "give_way":
            self.heading += 40  # 向右转 40°

class ErraticBehavior(TargetShipBehavior):
    """L4：不规则行为——模拟不遵守规则或不可预测的目标"""
    
    def step(self, dt):
        # 随机航向变化
        if np.random.random() < 0.01:  # 1% 概率突然转向
            self.heading += np.random.uniform(-60, 60)
        
        # 随机速度变化
        if np.random.random() < 0.005:
            self.speed *= np.random.uniform(0.5, 1.5)
        
        self.update_position(dt)
```

### 7.2 交通场景生成

```python
class TrafficGenerator:
    
    def generate_colregs_scenario(self, scenario_type, own_ship_position):
        """生成标准 COLREGs 会遇场景"""
        
        targets = []
        
        if scenario_type == "head_on":
            targets.append(TargetShip(
                position=offset(own_ship_position, bearing=0, distance=5000),
                course=180,  # 正对面来
                speed=6.0,
                mmsi=self.random_mmsi(),
                ship_type="cargo",
                behavior=ColregsCompliantBehavior()
            ))
        
        elif scenario_type == "crossing_from_starboard":
            targets.append(TargetShip(
                position=offset(own_ship_position, bearing=45, distance=4000),
                course=270,  # 从右舷 45° 横穿
                speed=5.0,
                behavior=ColregsCompliantBehavior()
            ))
        
        elif scenario_type == "overtaking":
            targets.append(TargetShip(
                position=offset(own_ship_position, bearing=0, distance=2000),
                course=0,  # 同向，速度较慢
                speed=3.0,
                behavior=StraightLineBehavior()
            ))
        
        elif scenario_type == "multi_target_conflict":
            # 3 艘目标从不同方向接近
            for bearing, course, distance in [(30, 250, 4000), (330, 110, 3500), (0, 180, 5000)]:
                targets.append(TargetShip(
                    position=offset(own_ship_position, bearing, distance),
                    course=course, speed=5.0 + np.random.uniform(-1, 1),
                    behavior=ColregsCompliantBehavior()
                ))
        
        elif scenario_type == "dense_fishing_fleet":
            # 15 艘渔船，低速，不规则运动
            for i in range(15):
                bearing = np.random.uniform(0, 360)
                distance = np.random.uniform(500, 3000)
                targets.append(TargetShip(
                    position=offset(own_ship_position, bearing, distance),
                    course=np.random.uniform(0, 360),
                    speed=np.random.uniform(0.5, 3.0),
                    ship_type="fishing",
                    has_ais=np.random.random() > 0.5,  # 50% 的渔船没有 AIS
                    behavior=ErraticBehavior()
                ))
        
        return targets
```

---

## 8. 海图与水域仿真

### 8.1 虚拟 ENC

```python
class VirtualENC:
    """虚拟电子海图——提供水深、海岸线、航标、TSS 等信息"""
    
    def __init__(self, chart_file=None):
        if chart_file:
            self.load_real_enc(chart_file)  # 加载真实 S-57 海图
        else:
            self.generate_synthetic_chart()  # 生成合成海图
    
    def generate_synthetic_chart(self):
        """生成用于测试的合成海图"""
        self.features = []
        
        # 添加海岸线
        self.features.append(Coastline(
            points=[(0, 10000), (5000, 10000), (8000, 8000), (10000, 5000)],
            type="natural"
        ))
        
        # 添加水深区域
        self.features.append(DepthArea(
            polygon=[(0, 0), (10000, 0), (10000, 10000), (0, 10000)],
            min_depth=20.0  # 大部分区域 20m 深
        ))
        self.features.append(DepthArea(
            polygon=[(7000, 7000), (9000, 7000), (9000, 9000), (7000, 9000)],
            min_depth=3.0  # 近岸浅水区
        ))
        
        # 添加航标
        self.features.append(Buoy(position=(2000, 5000), type="lateral_port", color="red"))
        self.features.append(Buoy(position=(2200, 5000), type="lateral_starboard", color="green"))
        
        # 添加 TSS
        self.features.append(TSSLane(
            direction=0,  # 北向
            center_line=[(3000, 0), (3000, 10000)],
            width=500  # m
        ))
        self.features.append(TSSLane(
            direction=180,  # 南向
            center_line=[(4000, 0), (4000, 10000)],
            width=500
        ))
    
    def get_depth(self, x, y):
        """查询某点的水深"""
        for feature in self.features:
            if isinstance(feature, DepthArea) and feature.contains(x, y):
                return feature.min_depth
        return 100.0  # 默认深水
    
    def get_zone_type(self, x, y):
        """查询某点的水域类型"""
        for feature in self.features:
            if isinstance(feature, TSSLane) and feature.contains(x, y):
                return "tss_lane"
        return "open_sea"
```

### 8.2 加载真实海图

```python
def load_real_enc(self, s57_file):
    """加载真实的 S-57 电子海图"""
    # 使用 GDAL/OGR 库解析 S-57 格式
    import osgeo.ogr as ogr
    
    ds = ogr.Open(s57_file)
    for layer_idx in range(ds.GetLayerCount()):
        layer = ds.GetLayer(layer_idx)
        layer_name = layer.GetName()
        
        if layer_name == "DEPARE":  # 水深区域
            for feature in layer:
                geom = feature.GetGeometryRef()
                drval1 = feature.GetField("DRVAL1")  # 最小水深
                self.features.append(DepthArea(geometry=geom, min_depth=drval1))
        
        # 类似地处理其他图层：COALNE, BOYCAR, BOYLAT, TSSRON, TSSLPT, ...
```

---

## 9. 通信仿真

```python
class CommSimulator:
    """模拟岸基通信的延迟、丢包和带宽限制"""
    
    def __init__(self, link_type="satellite"):
        if link_type == "satellite":
            self.latency_ms = 600        # 卫星延迟 ~600ms
            self.jitter_ms = 100         # 抖动 ±100ms
            self.packet_loss_rate = 0.02  # 2% 丢包率
            self.bandwidth_kbps = 256    # 带宽
        elif link_type == "4g":
            self.latency_ms = 50
            self.jitter_ms = 20
            self.packet_loss_rate = 0.001
            self.bandwidth_kbps = 10000
    
    def simulate_message(self, message, t):
        """模拟消息传输——返回接收时间和是否成功"""
        
        if np.random.random() < self.packet_loss_rate:
            return None  # 丢包
        
        delay = self.latency_ms + np.random.normal(0, self.jitter_ms)
        delivery_time = t + max(0, delay) / 1000
        
        return (delivery_time, message)
```

---

## 10. SIL/HIL 集成架构

### 10.1 SIL（Software-In-the-Loop）

MASS_ADAS 的全部 ROS2 节点运行在同一台计算机上（或 Docker 容器中），传感器数据由仿真引擎通过 ROS2 话题注入。

```
┌───────────────────────────────────┐
│         Simulation PC             │
│                                   │
│  ┌─────────────┐  ┌────────────┐ │
│  │ Sim Engine  │  │ MASS_ADAS  │ │
│  │ (Python/C++)│  │ (ROS2 nodes)│ │
│  │             │  │            │ │
│  │ → /radar    │──│→ Preproc   │ │
│  │ → /ais      │  │→ Detection │ │
│  │ → /camera   │  │→ Fusion    │ │
│  │ → /gps      │  │→ L3→L4→L5 │ │
│  │             │  │            │ │
│  │ ← /control  │──│← Actuator  │ │
│  └─────────────┘  └────────────┘ │
└───────────────────────────────────┘
```

### 10.2 HIL（Hardware-In-the-Loop）

L5 控制层运行在实际的嵌入式硬件上（如 NVIDIA Jetson），通过以太网连接仿真 PC。执行机构（舵机、主机）由电子负载模拟器替代。

```
┌─────────────────────┐          ┌─────────────────────┐
│   Simulation PC     │          │   Jetson / 实际硬件   │
│   (Sim Engine +     │  Ethernet│   (L5 Control +     │
│    L1~L4 ROS2)      │ ←────→  │    Hardware Override │
│                     │          │    + GPIO 接口)       │
└─────────────────────┘          └──────┬──────────────┘
                                        │ PWM / CAN
                                 ┌──────┴──────────────┐
                                 │  Actuator Simulator  │
                                 │  (电子负载 + 舵角反馈)│
                                 └─────────────────────┘
```

### 10.3 ROS2 时钟管理

```python
# SIL 仿真使用 /clock 话题控制 ROS2 时间
# 所有 MASS_ADAS 节点必须使用 use_sim_time=true 参数

# 实时模式
clock_rate = 1.0  # 1:1

# 加速模式
clock_rate = 10.0  # 10x 加速

# 暂停模式
clock_rate = 0.0  # 暂停

class SimClockManager:
    def __init__(self, rate=1.0):
        self.rate = rate
        self.sim_time = 0.0
        self.clock_publisher = create_publisher('/clock', Clock)
    
    def step(self, wall_dt):
        self.sim_time += wall_dt * self.rate
        msg = Clock()
        msg.clock = to_ros_time(self.sim_time)
        self.clock_publisher.publish(msg)
```

---

## 11. 场景管理系统

### 11.1 场景定义格式

```yaml
# scenario_001_head_on.yaml
scenario:
  id: "COL-001"
  name: "Standard Head-on Encounter"
  description: "两艘机动船正对面相遇"
  category: "colregs"
  difficulty: "basic"
  duration_s: 600  # 10 分钟
  
initial_conditions:
  own_ship:
    type: "USV_12m"
    position: {lat: 31.23, lon: 121.47}
    heading: 0
    speed_knots: 14
  
  targets:
    - mmsi: 123456789
      type: "Cargo_200m"
      position: {lat: 31.28, lon: 121.47}  # 正前方约 5.5km
      heading: 180
      speed_knots: 12
      has_ais: true
      behavior: "colregs_compliant"
  
environment:
  wind: {speed_knots: 10, direction: 90}
  wave: {Hs: 0.5, Tp: 6, direction: 90}
  current: {speed_knots: 0.5, direction: 180}
  visibility_nm: 10
  sea_state: 2

route:
  waypoints:
    - {lat: 31.23, lon: 121.47}
    - {lat: 31.35, lon: 121.47}

pass_criteria:
  - type: "min_cpa"
    threshold_m: 500
    description: "CPA 不小于 500m"
  - type: "no_collision"
    description: "不发生碰撞"
  - type: "colregs_compliance"
    description: "避让方向符合 COLREGs（向右转）"
  - type: "max_deviation_nm"
    threshold: 1.0
    description: "偏航不超过 1nm"
```

### 11.2 场景库结构

```
scenarios/
├── colregs/
│   ├── head_on/
│   │   ├── COL-001_basic.yaml
│   │   ├── COL-002_fuzzy_zone.yaml        # 模糊区（方位偏 12°）
│   │   └── COL-003_high_speed.yaml        # 高速对遇
│   ├── crossing/
│   │   ├── COL-010_starboard_45deg.yaml
│   │   ├── COL-011_starboard_90deg.yaml
│   │   └── COL-012_port_315deg.yaml
│   ├── overtaking/
│   │   ├── COL-020_from_astern.yaml
│   │   └── COL-021_being_overtaken.yaml
│   └── multi_target/
│       ├── COL-030_two_target_conflict.yaml
│       └── COL-031_three_target.yaml
├── weather/
│   ├── WTH-001_fog.yaml                   # 能见度不良
│   ├── WTH-002_heavy_rain.yaml
│   └── WTH-003_high_sea_state.yaml
├── waterway/
│   ├── WAT-001_narrow_channel.yaml
│   ├── WAT-002_tss.yaml
│   └── WAT-003_port_approach.yaml
├── emergency/
│   ├── EMR-001_sensor_failure.yaml
│   ├── EMR-002_rudder_stuck.yaml
│   ├── EMR-003_gps_spoofing.yaml
│   └── EMR-004_comms_loss.yaml
├── traffic/
│   ├── TRF-001_dense_shipping.yaml
│   ├── TRF-002_fishing_fleet.yaml
│   └── TRF-003_anchorage_area.yaml
└── regression/
    └── REG-001_full_voyage.yaml            # 完整航次端到端
```

---

## 12. 可视化与回放系统

### 12.1 实时可视化

```python
# 基于 Web 的实时可视化（使用 Leaflet.js 或 Mapbox）
# 显示内容：
# - 本船位置和航迹
# - 所有目标船位置、航迹、AIS 信息
# - 雷达探测范围圆
# - 安全走廊
# - CPA/TCPA 线
# - 避让航点
# - 环境条件（风向标、流向箭头）
# - 系统状态面板（各模块状态）
# - 实时指标（CTE、航向误差、舵角）

# 技术方案：
# Backend: ROS2 → WebSocket bridge (rosbridge_suite)
# Frontend: React + Leaflet.js + ECharts
```

### 12.2 回放系统

```python
class SimulationRecorder:
    """记录仿真过程用于离线回放和分析"""
    
    def __init__(self, output_dir):
        self.output_dir = output_dir
        self.rosbag_recorder = start_rosbag_recording(output_dir)
        self.metrics_log = open(f"{output_dir}/metrics.csv", "w")
    
    def record_step(self, t, own_ship, targets, environment, control, metrics):
        # 自动由 rosbag 记录所有 ROS2 话题
        # 额外记录仿真引擎的真值数据（ground truth）
        self.metrics_log.write(f"{t},{own_ship.x},{own_ship.y},{own_ship.psi},{metrics.cte},{metrics.min_cpa}\n")

class SimulationReplayer:
    """回放记录的仿真数据"""
    
    def replay(self, recording_dir, speed=1.0):
        # 使用 ros2 bag play 回放
        # 配合可视化系统做离线分析
        os.system(f"ros2 bag play {recording_dir} --rate {speed}")
```

---

## 13. 自动化测试框架

### 13.1 批量场景运行

```python
class AutomatedTestRunner:
    
    def run_test_suite(self, scenario_dir, ship_config):
        """批量运行场景并生成报告"""
        
        results = []
        
        for scenario_file in glob(f"{scenario_dir}/**/*.yaml", recursive=True):
            scenario = load_scenario(scenario_file)
            
            print(f"Running: {scenario.id} - {scenario.name}")
            
            # 运行仿真
            sim = Simulation(scenario, ship_config)
            result = sim.run()
            
            # 评估通过/失败
            passed = evaluate_pass_criteria(result, scenario.pass_criteria)
            
            results.append({
                "scenario_id": scenario.id,
                "name": scenario.name,
                "passed": all(passed.values()),
                "criteria_results": passed,
                "metrics": result.metrics,
                "duration": result.elapsed_time
            })
        
        # 生成报告
        report = generate_test_report(results)
        return report
```

### 13.2 CI/CD 集成

```yaml
# .github/workflows/simulation_tests.yml
name: Simulation Test Suite
on: [push, pull_request]

jobs:
  sim_tests:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - name: Build MASS_ADAS
        run: colcon build
      - name: Run L2 Sim Tests
        run: python test_runner.py --suite colregs --fidelity L2 --speed 50x
      - name: Run L3 Sim Tests
        run: python test_runner.py --suite full --fidelity L3 --speed 10x
      - name: Generate Report
        run: python generate_report.py --output test_results/
```

---

## 14. 评估指标与评分系统

### 14.1 核心指标

| 类别 | 指标 | 计算方法 | 目标值 |
|------|------|---------|-------|
| **安全** | 碰撞次数 | range < 5m 的事件 | 0 |
| | 最小 CPA | 所有会遇中的最小 CPA | ≥ CPA_safe |
| | COLREGs 违规次数 | Checker 否决次数 | 0 |
| **精度** | 平均 CTE | 稳态航行时的 RMS CTE | < 5m |
| | 航向误差 RMS | 稳态航行时的 RMS 航向误差 | < 1° |
| | ETA 偏差 | 实际到达时间 vs 计划 | < 30min |
| **效率** | 燃油消耗 | 总燃油消耗量 | ≤ 计划 × 1.1 |
| | 偏航距离 | 避碰导致的额外航程 | ≤ 直航 × 1.2 |
| | 舵角活动量 | ∫|dδ/dt|dt | 最小化 |
| **响应** | 避碰响应时间 | 从威胁出现到开始避让的时间 | < T_give_way |
| | 紧急响应时间 | 从紧急触发到执行的时间 | < 500ms |

### 14.2 综合评分

```python
def compute_scenario_score(metrics, weights=None):
    """计算场景综合评分（0~100）"""
    
    if weights is None:
        weights = {"safety": 0.4, "accuracy": 0.25, "efficiency": 0.2, "response": 0.15}
    
    safety_score = 100 if metrics.collision_count == 0 and metrics.min_cpa >= CPA_safe else 0
    
    accuracy_score = max(0, 100 - metrics.cte_rms * 5 - metrics.heading_error_rms * 20)
    
    efficiency_score = max(0, 100 - (metrics.fuel_ratio - 1.0) * 200 - (metrics.deviation_ratio - 1.0) * 100)
    
    response_score = 100 if metrics.response_time < T_give_way else max(0, 100 - (metrics.response_time - T_give_way) * 5)
    
    total = (weights["safety"] * safety_score + weights["accuracy"] * accuracy_score + 
             weights["efficiency"] * efficiency_score + weights["response"] * response_score)
    
    return total
```

---

## 15. 多船型参数化配置

系统通过配置文件切换船型，无需修改代码：

```python
# 启动仿真时指定船型
ros2 launch mass_adas_sim simulation.launch.py ship_config:=configs/usv_12m.yaml
ros2 launch mass_adas_sim simulation.launch.py ship_config:=configs/cargo_200m.yaml
ros2 launch mass_adas_sim simulation.launch.py ship_config:=configs/pilot_boat_40m.yaml
```

### 预定义船型库

| 船型 | 文件 | L(m) | B(m) | T(m) | Δ(t) | V_max(kn) |
|------|------|------|------|------|------|-----------|
| SANGO USV | usv_12m.yaml | 12 | 3.5 | 0.8 | 8 | 19.4 |
| 引航船 | pilot_40m.yaml | 40 | 9 | 3.5 | 400 | 16 |
| 沿海渡船 | ferry_80m.yaml | 80 | 16 | 4.5 | 3000 | 18 |
| 散货船 | bulk_200m.yaml | 200 | 32 | 12.5 | 50000 | 14.5 |
| 小型渔船 | fishing_15m.yaml | 15 | 4 | 1.5 | 20 | 10 |
| 大型集装箱船 | container_350m.yaml | 350 | 48 | 15 | 150000 | 22 |

---

## 16. 极端场景与对抗测试

### 16.1 极端场景

```yaml
# 多目标极端——20 艘船从各方向接近
extreme_001_20_targets:
  targets: 20
  distribution: "random_360deg"
  cpa_range: [200, 2000]
  speed_range: [3, 15]

# 传感器全部降级
extreme_002_sensor_cascade:
  events:
    - t: 60, type: "radar_failure"
    - t: 120, type: "gps_degraded", hdop: 8
    - t: 180, type: "camera_failure"
    # 只剩 AIS 和 IMU

# 不遵守规则的目标
extreme_003_non_compliant:
  target_behavior: "erratic"
  # 对方应该让路但不让，应该直行但突然转向
```

### 16.2 对抗测试（Adversarial）

```python
class AdversarialTargetBehavior:
    """对抗性行为——专门设计来考验 MASS_ADAS 的极端场景"""
    
    def step(self, dt, own_ship):
        # 始终朝向本船航行（最大碰撞风险）
        bearing_to_own = compute_bearing(self.position, own_ship.position)
        self.heading = bearing_to_own
        
        # 当本船开始避让时，跟随调整（最恶劣情况）
        if self.detect_own_ship_turning(own_ship):
            self.heading += own_ship.turn_direction * 10  # 朝同方向转
```

---

## 17. 分布式仿真与联合仿真

对于大规模场景（100+ 目标）或需要集成第三方仿真器的情况：

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│ MASS_ADAS Node  │     │ Traffic Sim     │     │ Environment Sim │
│ (ROS2)          │←───→│ (独立进程)       │←───→│ (独立进程)       │
│                 │ DDS │                 │ DDS │                 │
└─────────────────┘     └─────────────────┘     └─────────────────┘
                               │
                        ┌──────┴──────┐
                        │ Sim Manager │
                        │ (时钟同步)   │
                        └─────────────┘
```

---

## 18. 仿真数据管理

```
sim_data/
├── ship_configs/           # 船型配置文件
├── scenarios/              # 场景定义
├── enc_data/               # 海图数据（真实+合成）
├── weather_data/           # 气象数据
├── recordings/             # 仿真录制文件（rosbag2）
├── reports/                # 测试报告
├── ground_truth/           # 仿真真值数据
└── ai_training_data/       # AI 训练用的合成传感器数据
```

---

## 19. 仿真与真实系统的接口协议

**核心原则：** 仿真传感器输出的 ROS2 话题名称和消息类型与真实传感器驱动完全一致。MASS_ADAS 的 ROS2 节点通过 `use_sim_time` 参数区分仿真和真实环境，但功能代码完全相同。

```python
# 仿真模式启动
ros2 launch mass_adas bringup.launch.py use_sim_time:=true sensor_source:=sim

# 真实模式启动
ros2 launch mass_adas bringup.launch.py use_sim_time:=false sensor_source:=real

# 唯一的区别：传感器数据的来源（仿真引擎 vs 真实传感器驱动）
```

---

## 20. 开发路线图与工具链选型

### 20.1 推荐工具链

| 组件 | 推荐工具 | 替代方案 | 说明 |
|------|---------|---------|------|
| 动力学引擎 | Python + NumPy | C++ | V1 用 Python 快速原型，V2 用 C++ 优化性能 |
| 环境仿真 | 自研（Python） | OpenFOAM | 自研满足 L3 保真度，CFD 用于 L4/L5 |
| 雷达仿真 | 自研 | RadarSimPy | 基于雷达方程的参数化模型 |
| 摄像头渲染 | Unity HDRP | Unreal Engine | 照片级渲染用于 AI 训练 |
| 海图 | GDAL/OGR + S-57 | OpenCPN | 解析真实海图 |
| ROS2 中间件 | ROS2 Humble | ROS2 Jazzy | 与 MASS_ADAS 生产环境一致 |
| 可视化 | Foxglove Studio | RViz2 + Web | Foxglove 支持 Web 和多种话题 |
| 数据记录 | rosbag2 | MCAP | rosbag2 原生支持 |
| 测试框架 | pytest + 自研 Runner | | |
| CI/CD | GitHub Actions | GitLab CI | |

### 20.2 开发路线

```
Phase 1（4 周）：基础框架
  - 3DOF 动力学引擎（Nomoto + 基础非线性）
  - 简化传感器仿真（ARPA + AIS + 理想 GPS）
  - 5 个基础 COLREGs 场景
  - SIL 闭环验证

Phase 2（4 周）：环境与交通
  - 风/浪/流模型
  - COLREGs 合规的目标船行为
  - 50 个场景库
  - 批量测试框架
  - 基础可视化

Phase 3（4 周）：高保真度
  - 6DOF 动力学（含横摇）
  - 雷达杂波仿真
  - 真实海图加载
  - 多船型配置
  - HIL 集成

Phase 4（持续）：AI 训练与认证
  - 照片级摄像头渲染（Unity 集成）
  - 对抗测试场景
  - 安全认证用场景库（200+）
  - 分布式仿真
```

---

## 21. 内部子系统分解

| 子系统 | 职责 | 建议语言 |
|-------|------|---------|
| Ship Dynamics Engine | 3DOF/6DOF 动力学积分 | Python→C++ |
| Environment Engine | 风/浪/流/能见度模型 | Python |
| Sensor Simulator | 雷达/AIS/GPS/摄像头/LiDAR 仿真 | Python/C++ |
| Traffic Generator | 目标船生成和行为控制 | Python |
| Virtual ENC | 合成/真实海图加载和查询 | Python+GDAL |
| Comm Simulator | 通信延迟/丢包模拟 | Python |
| Scenario Manager | 场景加载/参数注入/时钟控制 | Python |
| Metrics Recorder | 指标采集和评分 | Python |
| Test Runner | 批量测试和报告生成 | Python |
| Visualizer | Web 实时可视化 + 回放 | TypeScript/React |
| Clock Manager | ROS2 仿真时钟 | C++ |

---

## 22. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| 动力学步长 | 0.01 s (100 Hz) | 内部积分步长 |
| 传感器输出步长 | 0.1 s (10 Hz) | 大部分传感器 |
| 雷达扫描周期 | 2.5 s | |
| AIS 最小报告间隔 | 2 s | |
| GPS 输出频率 | 1~10 Hz | |
| 默认仿真时长 | 600 s | |
| 最大加速倍率 | 100× | L1/L2 保真度 |
| 最大同时目标数 | 200 | |
| 场景库最小规模 | 200 | |

---

## 23. 测试策略与验收标准

| 指标 | 标准 |
|------|------|
| 动力学精度（旋回直径） | 与实船偏差 < 15% |
| 传感器噪声统计特性 | 与真实传感器方差比 0.8~1.2 |
| SIL 闭环稳定性 | 10 分钟无发散 |
| 场景通过率（基础） | ≥ 95% |
| 场景通过率（极端） | ≥ 80% |
| 批量测试性能 | 200 场景 < 2 小时（L2 保真度） |
| 回放一致性 | 相同场景相同种子 → 100% 可复现 |

---

## 24. 参考文献与标准

| 编号 | 文献 | 相关内容 |
|------|------|---------|
| [1] | Fossen, T.I. "Handbook of Marine Craft Hydrodynamics" 2021 | 船舶动力学建模 |
| [2] | ITTC Recommended Procedures 7.5-02-07-04 | 船舶操纵性仿真方法 |
| [3] | DNV-RP-C205 "Environmental Conditions and Environmental Loads" | 环境载荷模型 |
| [4] | IMO MSC.137(76) "Standards for Ship Manoeuvrability" | 操纵性标准（验证基准） |
| [5] | Nomoto, K. et al. "On the Steering Qualities of Ships" 1957 | Nomoto 模型 |
| [6] | Hasselmann, K. et al. "JONSWAP" 1973 | JONSWAP 波浪谱 |
| [7] | ROS2 Humble Documentation | ROS2 仿真集成 |
| [8] | Unity HDRP Documentation | 照片级渲染 |

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-26 | 架构组 | 初始版本 |

---

**文档结束**
