# MASS_ADAS Cybersecurity Architecture 网络安全架构技术设计文档

**文档编号：** SANGO-ADAS-CYB-001  
**版本：** 0.1 草案  
**日期：** 2026-04-26  
**编制：** 系统架构组  
**状态：** 开发参考——v2.0 防御性架构升级新增文档

---

## 目录

1. 概述与职责定义
2. 合规要求映射
3. 网络分区架构
4. DMZ 网关设计
5. 单向数据二极管
6. DDS-Security 配置
7. 节点认证与访问控制
8. 岸基指令认证流程
9. 入侵检测系统（IDS）
10. 安全事件日志与审计
11. 网络分区与实时性的平衡
12. 密钥管理
13. 通信中断时的安全降级
14. 物理网络拓扑
15. 内部子模块分解
16. 参数总览表
17. 与其他模块的协作关系
18. 测试策略与验收标准
19. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

Cybersecurity Architecture（网络安全架构）是 v2.0 防御性架构升级中新增的 Z 轴顶层防护。它在整个 MASS_ADAS 系统的网络通信入口处建立 IT/OT 硬隔离边界，确保岸基的 IT 网络（卫星通信、移动网络）中的威胁无法渗透到船端的 OT 控制网络（L1~L5 决策管线和 Perception 子系统）。

### 1.2 解决的核心痛点

原架构中，Shore Link 模块通过 ROS2 DDS 直接与系统内部话题通信。这意味着岸基卫星链路上的任何数据包——无论是合法的远程监控指令还是恶意伪造的控制命令——都可以直接到达 DDS 通信总线。攻击者如果攻破了卫星通信链路（如 VSAT 终端漏洞），理论上可以：

- 伪造航向指令让船转向危险方向
- 伪造 AIS 消息制造虚假目标，误导避碰决策
- 注入恶意 DDS 节点窃取船舶位置和航线数据
- 通过 DoS 攻击瘫痪 DDS 通信总线

2024 年 7 月起强制生效的 IACS UR E26（船舶网络韧性）和 UR E27（船上系统和设备网络韧性）要求所有新建船舶必须实施 IT/OT 隔离和网络安全管理。不满足此要求将无法获得船级社入级证书。

### 1.3 设计原则

- **纵深防御原则：** 不依赖单一安全措施——DMZ 网关 + 数据二极管 + DDS-Security + 节点认证多层防护。
- **最小权限原则：** 每个网络区域、每个 DDS 节点只能访问其功能所需的最少资源。
- **不影响实时性原则：** L5 控制层的 50Hz 实时回路不受安全加密影响——通过网络分区而非加密来保护实时总线。
- **失败安全原则：** 网络安全系统本身的故障不应影响船舶的基本操控——安全系统离线时系统切换到最保守的自主模式。

---

## 2. 合规要求映射

### 2.1 IACS UR E26 — 船舶网络韧性

| UR E26 要求 | MASS_ADAS 实现 |
|------------|---------------|
| 5.1 识别资产并划分安全区域 | IT/OT/RT 三区划分（见第 3 节） |
| 5.2 实施访问控制 | DDS-Security 节点认证 + 话题级访问控制（见第 7 节） |
| 5.3 检测网络安全事件 | IDS 入侵检测（见第 9 节） |
| 5.4 响应网络安全事件 | 安全事件响应流程（见第 10 节） |
| 5.5 恢复正常运行 | 安全降级与恢复（见第 13 节） |

### 2.2 IACS UR E27 — 船上系统和设备网络韧性

| UR E27 要求 | MASS_ADAS 实现 |
|------------|---------------|
| 4.1 安全设计——无不必要的网络服务 | 所有 DDS 节点只开放必要话题 |
| 4.2 安全设计——默认安全配置 | 出厂配置即为安全配置（白名单模式） |
| 4.3 安全设计——通信完整性保护 | DDS-Security 消息签名 |
| 4.4 安全更新机制 | OTA 安全更新通过 DMZ 认证链（见第 12 节） |
| 4.5 安全日志 | 安全事件日志（见第 10 节） |

---

## 3. 网络分区架构

### 3.1 三区划分

```
┌─────────────────────────────────────────────────────┐
│                 IT ZONE (岸基/外部)                    │
│   卫星终端、4G/5G 模组、WiFi AP                       │
│   岸基调度系统、远程监控中心（ROC）                    │
│   风险等级：高（暴露在互联网/公网中）                  │
└──────────────────────┬──────────────────────────────┘
                       │
                ┌──────┴──────┐
                │  DMZ ZONE   │
                │  DMZ 网关    │  ← 防火墙 + IDS + 协议清洗
                │  数据二极管  │  ← 物理单向（OT→IT 只读）
                └──────┬──────┘
                       │
┌──────────────────────┴──────────────────────────────┐
│                 OT ZONE (船端控制)                     │
│   ┌─────────────────────────────────────────────┐    │
│   │  DDS Network (ROS2)                          │    │
│   │  L1 Mission → L2 Voyage → L3 Tactical        │    │
│   │  → L4 Guidance → L5 Control (DDS 部分)        │    │
│   │  Perception 子系统                            │    │
│   │  Checker Track                                │    │
│   │  DDS-Security 保护（节点认证+话题加密）       │    │
│   └─────────────────────┬───────────────────────┘    │
│                         │                             │
│   ┌─────────────────────┴───────────────────────┐    │
│   │  RT ZONE (实时控制总线)                       │    │
│   │  CAN Bus / EtherCAT                          │    │
│   │  L5 → 舵机、主机、侧推器                     │    │
│   │  硬线仲裁器（Override Arbiter）               │    │
│   │  物理隔离——不接入 DDS 网络                   │    │
│   └─────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────┘
```

### 3.2 区域间通信规则

| 通信方向 | 允许？ | 机制 | 说明 |
|---------|-------|------|------|
| IT → DMZ | 是 | 防火墙白名单端口 | 只允许认证的指令协议 |
| DMZ → OT | 是（受限） | 协议清洗 + 指令认证 | 只允许认证后的航次指令和远程操控指令 |
| OT → DMZ | 是 | 数据二极管 | 单向：遥测数据、日志上传 |
| DMZ → IT | 是 | 数据二极管出口 | 上传岸基 |
| IT → OT | **否** | 物理阻断 | IT 不可直接访问 OT |
| OT → RT | 是 | 网关（单向或双向受控） | L5 DDS → CAN/EtherCAT 转换 |
| RT → OT | 是（受限） | 传感器反馈 | 执行机构状态回传 |

---

## 4. DMZ 网关设计

### 4.1 功能架构

```
FUNCTION dmz_gateway_process(incoming_packet):
    
    # 1. 防火墙层——基于 IP/端口/协议的过滤
    IF NOT firewall.allow(incoming_packet):
        log_security_event("firewall_blocked", incoming_packet.src)
        DROP incoming_packet
        RETURN
    
    # 2. 协议清洗——只允许已知的应用协议
    IF incoming_packet.protocol NOT IN ALLOWED_PROTOCOLS:
        log_security_event("unknown_protocol_blocked", incoming_packet.protocol)
        DROP incoming_packet
        RETURN
    
    # 3. 深度包检查（DPI）——检查载荷内容
    IF dpi.detect_anomaly(incoming_packet.payload):
        log_security_event("dpi_anomaly_detected", incoming_packet)
        DROP incoming_packet
        RETURN
    
    # 4. 指令认证——验证数字签名
    IF incoming_packet.type == "command":
        IF NOT verify_command_signature(incoming_packet):
            log_security_event("auth_failed", incoming_packet)
            DROP incoming_packet
            alert_shore("SECURITY: 未认证指令被拒绝")
            RETURN
    
    # 5. 速率限制——防止 DoS
    IF rate_limiter.exceeds(incoming_packet.src):
        log_security_event("rate_limit_exceeded", incoming_packet.src)
        DROP incoming_packet
        RETURN
    
    # 6. 通过所有检查——转发到 OT 网络
    forward_to_ot(incoming_packet)

ALLOWED_PROTOCOLS = ["SANGO_CMD_v1", "SANGO_TELEMETRY_v1", "NTP", "PTP"]
```

### 4.2 DMZ 网关硬件选型建议

| 要求 | 规格 |
|------|------|
| 处理能力 | 1000 pkt/s（远超卫星通信速率，留足余量） |
| 网络接口 | 2 × Ethernet（IT 侧 + OT 侧），物理隔离 |
| 安全功能 | 有状态防火墙 + DPI + IDS + 日志 |
| 可靠性 | 工业级（-20~60°C，防震防潮） |
| 冗余 | 双机热备（可选） |
| 候选产品 | Moxa EDF-G1002-BP、Cisco IE-3400、或定制 Linux 防火墙 |

---

## 5. 单向数据二极管

### 5.1 原理

数据二极管是一种物理上保证单向通信的硬件设备。它通过在一个方向上移除物理信号线（如只保留光纤的发送端而切断接收端）来实现硬件级的单向保证——即使软件被完全攻破，也无法反向发送数据。

### 5.2 在 MASS_ADAS 中的应用

```
OT 网络 ──[发送]──→ 数据二极管 ──[接收]──→ DMZ ──→ IT 网络

传输内容：
  - 遥测数据（位置、航向、航速）
  - 态势图（目标列表、环境状态）
  - 系统健康状态
  - ASDR 日志（定期批量上传）
  - 避碰事件报告

不可反向传输的保证：
  - 岸基无法通过此通道向 OT 注入任何数据
  - 即使二极管后端的 DMZ 软件被攻破，也无法写入 OT
```

### 5.3 反向通信（IT→OT）的唯一路径

岸基向船端发送指令的唯一路径是通过 DMZ 网关的正向通道（经过完整的认证和清洗链）。不存在绕过 DMZ 的替代路径。

---

## 6. DDS-Security 配置

### 6.1 DDS-Security 概述

DDS-Security 是 OMG 标准（DDS Security v1.1），为 DDS 通信提供以下安全能力：

| 安全能力 | 实现方式 | MASS_ADAS 中的应用 |
|---------|---------|------------------|
| 认证（Authentication） | 基于 PKI 证书的节点身份验证 | 每个 ROS2 节点需有效证书才能加入 DDS 网络 |
| 访问控制（Access Control） | 基于 XML 策略的话题级权限 | 每个节点只能发布/订阅其功能所需的话题 |
| 加密（Encryption） | AES-GCM 对消息载荷加密 | 跨区域通信加密；OT 内部可选 |
| 完整性（Integrity） | HMAC 消息签名 | 所有消息带签名，防篡改 |
| 日志（Logging） | 安全事件记录 | 认证失败、权限拒绝等事件记录 |

### 6.2 加密范围——实时性考量

```
# 关键决策：哪些通信需要加密，哪些不需要

# 必须加密（跨区域或含敏感信息）：
ENCRYPT:
  - DMZ ↔ L1（岸基指令）
  - Shore Link 上行遥测
  - 安全事件日志

# 仅签名不加密（OT 内部，需要完整性但延迟敏感）：
SIGN_ONLY:
  - L1 → L2（航次任务）
  - L2 → L3（航线数据）
  - L3 → L4（避碰指令）
  - Perception → L3（态势数据）

# 不加密不签名（RT 实时控制，延迟极敏感）：
PLAINTEXT:
  - L5 内部 50Hz 控制回路
  - L5 → 执行机构（CAN Bus）
  - Nav Filter → L4/L5（50Hz 位姿反馈）
  
  ↑ 这些通信在 OT 内部的 RT Zone，已被网络分区保护
```

### 6.3 DDS-Security 对延迟的影响

| 安全级别 | 附加延迟 | 适用场景 |
|---------|---------|---------|
| 完整加密（AES-GCM + HMAC） | 0.5~2 ms/msg | 跨区域通信 |
| 仅签名（HMAC-SHA256） | 0.1~0.5 ms/msg | OT 内部非实时话题 |
| 无安全（明文） | 0 ms | RT Zone 内部 |

对于 L5 的 50Hz 回路（周期 20ms），即使 0.5ms 的签名延迟也会占用 2.5% 的周期——可以接受。但 2ms 的加密延迟占 10%——不建议在 50Hz 回路中使用。

---

## 7. 节点认证与访问控制

### 7.1 PKI 证书架构

```
Root CA（SANGO 根证书颁发机构）
  │
  ├── Ship CA（船舶级 CA——每艘船独立）
  │     │
  │     ├── L1_Mission_Node.cert
  │     ├── L2_Voyage_Planner_Node.cert
  │     ├── L3_Tactical_Node.cert
  │     ├── L4_Guidance_Node.cert
  │     ├── L5_Control_Node.cert
  │     ├── Perception_Node.cert
  │     ├── Checker_Node.cert
  │     └── ASDR_Node.cert
  │
  └── Shore CA（岸基 CA）
        │
        ├── ROC_Operator.cert（远程操作员）
        └── Dispatch_System.cert（调度系统）
```

### 7.2 话题级访问控制策略

```xml
<!-- DDS-Security Access Control Policy (示例) -->
<dds_security>
  <domain_access_rules>
    
    <!-- L3 Tactical Node 的权限 -->
    <grant name="L3_Tactical">
      <allow_rule>
        <!-- 允许订阅的话题 -->
        <subscribe>
          <topics><topic>/perception/targets</topic></topics>
          <topics><topic>/nav/odom</topic></topics>
          <topics><topic>/mission/voyage_task</topic></topics>
        </subscribe>
        <!-- 允许发布的话题 -->
        <publish>
          <topics><topic>/decision/tactical/*</topic></topics>
          <topics><topic>/system/emergency_maneuver</topic></topics>
        </publish>
      </allow_rule>
      <!-- 隐式拒绝所有未列出的话题 -->
      <default_rule>DENY</default_rule>
    </grant>

    <!-- L5 Control Node 不允许发布到 L1/L2/L3 话题 -->
    <grant name="L5_Control">
      <allow_rule>
        <subscribe>
          <topics><topic>/guidance/*</topic></topics>
          <topics><topic>/system/emergency_maneuver</topic></topics>
        </subscribe>
        <publish>
          <topics><topic>/control/*</topic></topics>
        </publish>
      </allow_rule>
      <default_rule>DENY</default_rule>
    </grant>

  </domain_access_rules>
</dds_security>
```

---

## 8. 岸基指令认证流程

```
FUNCTION authenticate_shore_command(command_msg):
    
    # 1. TLS 通道验证（传输层）
    IF NOT tls_session.is_valid():
        RETURN REJECT("TLS 会话无效")
    
    # 2. 数字签名验证（应用层）
    signature = command_msg.signature
    payload = command_msg.payload
    shore_cert = command_msg.certificate
    
    # 验证证书链
    IF NOT verify_cert_chain(shore_cert, root_ca):
        RETURN REJECT("证书链验证失败")
    
    # 验证签名
    IF NOT rsa_verify(payload, signature, shore_cert.public_key):
        RETURN REJECT("签名验证失败")
    
    # 3. 时间戳有效性（防重放攻击）
    IF abs(now() - command_msg.timestamp) > REPLAY_WINDOW:
        RETURN REJECT("消息时间戳过期（可能是重放攻击）")
    
    # 4. 序列号检查（防重复）
    IF command_msg.sequence_number ≤ last_accepted_sequence:
        RETURN REJECT("序列号已使用（重复消息）")
    
    # 5. 权限检查
    IF NOT check_permission(shore_cert.role, command_msg.type):
        RETURN REJECT("权限不足")
    
    # 全部通过
    last_accepted_sequence = command_msg.sequence_number
    log_security_event("command_authenticated", command_msg)
    RETURN ACCEPT

REPLAY_WINDOW = 60    # 秒——消息有效窗口
```

---

## 9. 入侵检测系统（IDS）

```
FUNCTION ids_monitor(network_traffic):
    
    # 规则 1：异常流量模式检测
    IF traffic_rate > BASELINE × 3:
        alert("ANOMALY: 流量异常激增", severity="HIGH")
    
    # 规则 2：未知 DDS 节点发现
    IF new_dds_participant_discovered AND NOT in_whitelist:
        alert("INTRUSION: 未授权 DDS 节点", severity="CRITICAL")
        # 自动隔离该节点
        block_participant(new_participant.guid)
    
    # 规则 3：认证失败频率
    IF auth_failure_count_last_5min > 10:
        alert("BRUTE_FORCE: 频繁认证失败", severity="HIGH")
        # 临时锁定来源 IP
        block_source(failure_source_ip, duration=300)
    
    # 规则 4：话题权限违规
    IF topic_access_denied_count > 5:
        alert("POLICY_VIOLATION: 频繁权限拒绝", severity="MEDIUM")
    
    # 规则 5：AIS 欺骗检测
    IF ais_message.position diverges_from radar_detection by > 1nm:
        alert("AIS_SPOOF: AIS 位置与雷达不一致", severity="HIGH")
```

---

## 10. 安全事件日志与审计

```
SecurityEventLog:
    Time timestamp
    string event_type          # "auth_failure"/"intrusion"/"policy_violation"/"anomaly"
    string severity            # "INFO"/"MEDIUM"/"HIGH"/"CRITICAL"
    string source              # 事件来源（IP/节点ID/端口）
    string details             # 事件详情
    string action_taken        # 采取的响应动作
    bytes raw_packet           # 原始数据包（用于取证分析）

# 日志存储：
# - 本地加密存储（OT 网络内，ASDR 的一部分）
# - 通过数据二极管上传岸基安全监控中心
# - 保留期限：至少 3 年（满足 ISM 审计要求）
```

---

## 11. 网络分区与实时性的平衡

### 11.1 核心策略

```
# L5 内部的 50Hz 控制回路——不过 DDS 网络

L5 Heading Controller ──[CAN Bus]──→ 舵机 ECU
L5 Speed Controller  ──[CAN Bus]──→ 主机 ECU
L5 Thrust Allocator  ──[CAN Bus]──→ 侧推 ECU

# 这些通信走 RT Zone 的物理隔离总线，不经过 DDS
# 因此 DDS-Security 的加密延迟完全不影响 L5 实时性

# L5 与 L4 的接口——走 DDS（OT Zone 内部）
# 使用 SIGN_ONLY 模式（0.1~0.5ms 延迟，可接受）
```

### 11.2 延迟预算分析

| 通信路径 | 安全级别 | 附加延迟 | 占周期比 | 可接受？ |
|---------|---------|---------|---------|---------|
| L4→L5（10Hz，DDS） | SIGN_ONLY | 0.3 ms | 0.3% | 是 |
| L5 内部（50Hz，DDS） | SIGN_ONLY | 0.3 ms | 1.5% | 是 |
| L5→舵机（50Hz，CAN） | PLAINTEXT | 0 ms | 0% | 是 |
| Shore→L1（事件，DDS） | FULL_ENCRYPT | 1.5 ms | N/A | 是（非实时） |
| Perception→L3（2Hz） | SIGN_ONLY | 0.3 ms | 0.06% | 是 |

---

## 12. 密钥管理

```
# 证书生命周期管理

certificate_lifecycle:
    generation:   在安全环境中离线生成（岸基 HSM）
    distribution: 通过物理安全通道（USB 密钥盘）部署到船端
    rotation:     每 12 个月轮换（通过 DMZ 认证的 OTA 更新）
    revocation:   岸基 CA 维护 CRL（证书吊销列表），船端定期同步
    storage:      船端密钥存储在 TPM（可信平台模块）中，防止物理提取

# 紧急密钥恢复
IF all_certificates_expired AND no_shore_contact:
    # 使用出厂预置的紧急证书（有效期 30 天）
    # 只允许基本操纵，不允许远程接管
    activate_emergency_certificate()
```

---

## 13. 通信中断时的安全降级

```
FUNCTION handle_comms_loss_security(comms_status):
    
    IF comms_status == "shore_link_lost":
        # 岸基通信中断——但船端 OT 网络正常
        # 安全影响：无法接收岸基指令，无法上传日志
        # 应对：切换到全自主模式，本地缓存日志
        security_mode = "autonomous_secure"
        log_locally_until_reconnect()
    
    IF comms_status == "dmz_gateway_failure":
        # DMZ 网关故障——IT 和 OT 完全隔离
        # 安全影响：同岸基通信中断
        # 额外影响：无法接收 NTP 时间同步
        security_mode = "autonomous_secure"
        switch_to_gps_time_sync()    # 使用 GPS PPS 作为时间源
    
    IF comms_status == "dds_security_failure":
        # DDS-Security 子系统故障——节点无法认证
        # 安全影响：新节点无法加入，但已认证节点可继续
        # 应对：不允许新节点加入，维持现有通信
        security_mode = "locked_down"
        freeze_dds_participants()    # 冻结 DDS 参与者列表
```

---

## 14. 物理网络拓扑

```
物理网络拓扑：

┌─────────────┐
│ 卫星终端     │ ← VSAT / Iridium / Starlink
│ 4G/5G 模组   │
└──────┬──────┘
       │ Ethernet (IT VLAN)
       │
┌──────┴──────┐
│ DMZ 网关     │ ← 工业防火墙（双网口，IT 侧 + OT 侧）
│ + IDS        │
│ + 数据二极管 │
└──────┬──────┘
       │ Ethernet (OT VLAN)
       │
┌──────┴──────────────────────────────────┐
│ OT 交换机（工业级管理型）                 │
│ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ │
│ │ IPC │ │ IPC │ │ GPU │ │ IMU │ │Radar│ │
│ │ L1-4│ │ L5  │ │Perc.│ │     │ │     │ │
│ └──┬──┘ └──┬──┘ └─────┘ └─────┘ └─────┘ │
│    │       │                              │
└────┼───────┼──────────────────────────────┘
     │       │
     │  ┌────┴────┐
     │  │ CAN Bus │ ← RT Zone（物理隔离）
     │  │ Gateway │
     │  └────┬────┘
     │       │
     │  ┌────┴────────────────┐
     │  │ 舵机 │ 主机 │ 侧推  │ ← 执行机构
     │  └─────────────────────┘
     │
┌────┴────┐
│Override │ ← 硬线仲裁器（继电器，独立于所有网络）
│Arbiter  │
└─────────┘
```

---

## 15. 内部子模块分解

| 子模块 | 职责 | 实现方式 |
|-------|------|---------|
| DMZ Gateway | 防火墙 + DPI + 速率限制 | 工业防火墙硬件 |
| Data Diode | 单向物理隔离 | 专用硬件（光纤单向） |
| DDS-Security Manager | 证书管理 + 节点认证 + 话题权限 | ROS2 SROS2 框架 |
| IDS Engine | 入侵检测规则引擎 | Suricata/Snort 定制规则 |
| Command Authenticator | 岸基指令认证（签名+序列号+时间戳） | C++ 自研 |
| Security Logger | 安全事件日志 + 加密存储 | C++/Python |
| Key Manager | 证书生命周期管理 + TPM 接口 | C++ |

---

## 16. 参数总览表

| 参数 | 默认值 | 说明 |
|------|-------|------|
| DDS-Security 跨区域加密 | AES-128-GCM | |
| DDS-Security OT 内部 | HMAC-SHA256（仅签名） | |
| RT Zone 安全级别 | 明文（物理隔离保护） | |
| 证书轮换周期 | 12 个月 | |
| 消息重放窗口 | 60 秒 | |
| IDS 认证失败锁定阈值 | 10 次/5 分钟 | |
| IDS 流量异常倍数 | 3× 基线 | |
| 安全日志保留期 | 3 年 | |
| DMZ 网关处理能力 | ≥ 1000 pkt/s | |

---

## 17. 与其他模块的协作关系

| 交互方 | 方向 | 数据 |
|-------|------|------|
| Shore Link → DMZ → L1 Voyage Order | 指令 | 认证后的航次指令 |
| L1~Perception → DMZ → Shore Link | 遥测 | 经数据二极管的态势和状态上传 |
| DDS-Security → 所有 ROS2 节点 | 控制 | 节点认证 + 话题权限 |
| IDS → ASDR | 日志 | 安全事件记录 |
| IDS → Risk Monitor | 告警 | 网络安全事件影响的操纵安全评估 |

---

## 18. 测试策略与验收标准

| 场景 | 描述 | 验收标准 |
|------|------|---------|
| CYB-001 | 合法岸基指令认证 | 认证通过 + 指令传递至 L1 |
| CYB-002 | 伪造签名的指令 | 认证拒绝 + 安全告警 |
| CYB-003 | 重放攻击（重复序列号） | 拒绝 + 告警 |
| CYB-004 | 未授权 DDS 节点加入 | IDS 检测 + 自动隔离 |
| CYB-005 | DDS 话题越权访问 | 权限拒绝 + 日志记录 |
| CYB-006 | DoS 攻击（高速率数据包） | 速率限制生效 + 源 IP 封锁 |
| CYB-007 | DMZ 网关故障 | 系统切换到全自主模式 |
| CYB-008 | 数据二极管反向注入测试 | 物理不可能 |
| CYB-009 | L5 控制回路延迟（含 DDS-Security） | 附加延迟 < 0.5ms |
| CYB-010 | 证书过期处理 | 紧急证书激活 + 降级运行 |

---

## 19. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | IACS UR E26 (Rev.1, 2022) | 船舶网络韧性 |
| [2] | IACS UR E27 (Rev.1, 2022) | 船上系统和设备网络韧性 |
| [3] | OMG DDS Security v1.1 | DDS 安全规范 |
| [4] | IEC 62443 | 工业自动化控制系统安全 |
| [5] | BIMCO "Guidelines on Cyber Security Onboard Ships" v4 | 船舶网络安全指南 |
| [6] | ROS2 SROS2 Documentation | ROS2 安全框架 |
| [7] | IMO MSC-FAL.1/Circ.3/Rev.2 | 船舶网络风险管理指南 |

---

**文档结束**
