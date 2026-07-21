# P0 Spec: manifest 几何修正 + Nomoto 字段语义澄清

> **产出**: brainstorming,2026-07-16
> **方案包**: `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md`
> **决策树日志**: `docs/superpowers/design-logs/2026-07-16-m5-mpc-colav-design-log.md`
> **关联裁决**: DP-02(VR-02)+ TBD-5(VR-TBD5)
> **范围**: M5 MPC 避碰重构 7 子项目的第 0 个(前置小修)。VDM 删除推到 P2。

---

## 目的

修正 M5 vessel manifest 与实际 FCB 船型的严重偏差(28m/95t vs 实际 45m/130-160T),并澄清 Nomoto 参数字段语义(字段名 `nomoto_K_inv_s` 与头注释矛盾)。这是 DP-02(Nomoto-扩展预测模型)和 TBD-5(Nomoto 参数辨识)的前置数据修正。

## 背景(来自方案包 + 探索证据)

- **manifest 几何偏差** [R22]: 现值 length=28.0/beam=6.5/draft=1.4/mass=95000(95t),实际 FCB(数据表+建造规格)LOA=45m/LBP=44.1m/beam=8.0m/draft=1.55m/排水量~130-160T。
- **Nomoto 字段语义歧义** [R22][探索 Q3]: 字段名 `nomoto_K_inv_s`(`_inv_s` 暗示 1/K)与 `nomoto_fallback.hpp:67` 头注释"Nomoto rudder gain [1/s]"(暗示 K)矛盾;值 0.08 在任何 K/1/K 运算中都没用到(NomotoFallback δ=0 使 K 项消失)→ 代码不可解析,须领域决定。
- **T_s 偏大** [R22]: 现值 15.0s 偏 [R22] 缩律估算范围上沿(T≈2-10s,L/U≈4.76s × T' 0.5-2)。
- **VDM 不在 P0 范围**: 探索发现 VDM 有真实结构依赖(TrajectoryPropagator::propagate_own 调 dynamics.step()),虽无生产 caller,但删除须先处置 propagate_own → 推到 P2(预测层重构时一并处理)。

## 用户裁决(brainstorming 澄清)

- **字段语义**: 存 K 本身(重命名 `nomoto_K_inv_s` → `nomoto_K_s`),与 Nomoto 模型 Tṙ+r=Kδ 一致 [用户 2026-07-16]
- **mass_kg**: 145000 kg(排水量 130-160T 中值)[用户 2026-07-16]
- **T_s 重估**: 6.0 s([R22] 缩律估算中值,T'≈1.26 × L/U≈4.76s)[用户 2026-07-16]
- **VDM 删除**: 推到 P2,不在 P0 范围 [用户 2026-07-16]

## 设计

### 改动范围(6 文件)

P0 是**纯配置 + 局部消费代码 + 测试 fixture** 的低风险修正,不触碰 NLP/BC/约束/VDM 逻辑。改动范围经消费者链探索验证(见"自闭环验证"节)。

| 文件 | 改动 |
|---|---|
| `config/fcb_vessel_capability.yaml` | 几何参数修正 + Nomoto 字段重命名/重估(yaml key `K_inv_s`→`K_s`) |
| `include/.../shared/capability_manifest.hpp` | 字段重命名 `nomoto_K_inv_s` → `nomoto_K_s` + 注释;**几何默认值一并更新**(length 28→45/beam 6.5→8.0/draft 1.4→1.55/mass 95000→145000,虽永不生效但保持一致性避免误导) |
| `src/shared/capability_manifest.cpp` | **loader 跟随**: `yaml_get` key `"K_inv_s"` → `"K_s"`(探索证实这是第 3 必改点,否则静默回退默认值) |
| `src/mid_mpc/nomoto_fallback.cpp` | 跟随字段名(成员初始化 `nomoto_K_inv_s_` → `nomoto_K_s_`) |
| `include/.../mid_mpc/nomoto_fallback.hpp` | 跟随字段名 + 头注释("rudder gain K [1/s]") |
| `test/fixtures/fcb_capability_fixture.yaml` | **测试 fixture 跟随**(镜像新值:几何 45/8.0/1.55/145000 + Nomoto T_s 6.0/K_s 0.3);被 test_vessel_dynamics_model.cpp + test_nomoto_fallback.cpp 加载 |

**明确排除**(推到 P2):
- VesselDynamicsModel(4-DOF MMG)删除
- TrajectoryPropagator::propagate_own 处置
- Nomoto 接入 NLP 预测模型
- T',K' 无量纲重构(P0 先存有量纲 K,T)

### 参数变更明细

**1. 几何参数**(基于 [R22] FCB 文档实测):

| 字段 | 现值 | 新值 | 依据 |
|---|---|---|---|
| `geometry.length_m` | 28.0 | **45.0** | LOA(LBP 44.1,取 LOA 作代表)[R22] |
| `geometry.beam_m` | 6.5 | **8.0** | 数据表 [R22] |
| `geometry.draft_m` | 1.4 | **1.55** | 建造规格 [R22] |
| `geometry.mass_kg` | 95000.0 | **145000.0** | 排水量 130-160T 中值 [用户裁决],标 [TBD-HAZID] inclining 校准 |

**2. Nomoto 参数**(字段重命名 + 重估):

| 字段 | 现值 | 新值 | 依据 |
|---|---|---|---|
| `nomoto.T_s` | 15.0 | **6.0** | [R22] 缩律估算中值(T'≈1.26 × L/U≈4.76s,范围 2-10s)[用户裁决],标 [TBD-HAZID] 海试校准 |
| `nomoto.K_inv_s` | 0.08 | **删除** | 字段名歧义,重命名 |
| `nomoto.K_s` | (不存在) | **0.3** | 新增,存 K 本身(单位 1/s),[R22] 数量级中值(K≈0.1-0.6/s)[用户裁决],标 [TBD-HAZID] 海试校准 |

**3. MMG 系数**: 保持不变(surge_added_mass_factor=0.05/sway=0.40/yaw=0.07,Yasukawa 文献估算 [R22],VDM 在 P2 删除时一并处理)。

### 数据流

```
yaml(geometry + nomoto.K_s/T_s)
  → CapabilityManifest 解析(capability_manifest.hpp 字段 nomoto_K_s)
    → NomotoFallback 存储(nomoto_fallback.cpp 成员,δ=0 不运算)
      → (P2 后) NLP Nomoto 预测消费 K,T
```

P0 阶段 K 值只存储不运算(NomotoFallback δ=0),重命名+改值**不影响任何运行时行为**(behavior-preserving)。

### 错误处理

- **yaml 解析一致性(关键 — 静默错误风险)**: 探索证实 loader 用 `yaml_get<double>` 带默认值回退,**缺失/未知 key 静默回退到 header 默认值,不报错不警告**。若只改 yaml 的 `K_inv_s`→`K_s` 而漏改 loader(capability_manifest.cpp 的 key)→ 静默加载默认值(0.08 或新默认),**不报错但值错**。→ 实现须**严格成对更新**:yaml key + loader key + hpp 字段 + nomoto_fallback 成员,缺一不可。单测必须断言**解析出的值 == yaml 写入值**(非默认值),以捕获静默回退。
- **解析校验**: 现有 loader 无 range/sign/NaN 校验(仅校验 vessel_id 非空)。P0 不新增校验逻辑(范围校验属 P2 Nomoto 接入时做),但单测须覆盖正常解析路径。
- **fixture 同步**: test fixture 必须与生产 yaml 同步改值,否则新断言读不到新值。

### 自闭环验证(关键 — 回答"P0 是否独立可验,不把错误带到集成")

经消费者链探索(read-only agent,2026-07-16),确认 P0 改值**运行时 behavior-preserving**:

| 改动字段 | 消费者 | 是否活路径? | 改值影响运行时? |
|---|---|---|---|
| length_m 28→45 | VDM compute_izz/compute_accelerations | ✗ VDM 无生产 caller(MidMpcNode 构造但不调;propagate_own 仅测试) | **否** |
| mass_kg 95000→145000 | VDM compute_izz((1/12)·m·L²)+ compute_accelerations(m/M) | ✗ 同上 | **否**(虽 Izz/加速度会变,但无活 caller) |
| beam_m / draft_m | **无任何消费者**(连 VDM 都不读) | — | **否**(纯元数据) |
| T_s 15→6.0 | NomotoFallback integrate_branch(`r -= dt/T·r`) | △ 有算术,但活路径 r₀=0/δ=0 → r≡0 → T 项消失 | **否** |
| K_inv_s→K_s 0.08→0.3 | NomotoFallback(纯存储,头注释"stored for future use") | ✗ 无任何运算 | **否** |
| ROS2 发布 | 无 manifest 几何/mass/nomoto 字段进任何消息(SAT3 轨迹用 own_ship.u,不用 manifest) | — | **否** |

→ **运行时自闭环确认:改值不改变任何生产路径输出**。风险仅在测试层(VDM/NomotoFallback 测试用新 fixture 会算出新数值),须回归测试覆盖。

## 测试

### 新增测试(P0 自闭环核心)

1. **manifest 解析单测**(扩展或新增;从生产 yaml 路径或更新后的 fixture 加载):
   - 验证 `nomoto_K_s` 字段正确解析为 **0.3**(断言 == yaml 值,非默认值,捕获静默回退)
   - 验证旧字段 `K_inv_s` 不再存在于 yaml/struct
   - 验证 `nomoto_T_s` 正确解析为 **6.0**
   - 验证 length=**45.0**/beam=**8.0**/draft=**1.55**/mass=**145000** 正确加载(断言精确值)

2. **消费者不受影响回归测试**(证明改值不意外影响消费者 —— 自闭环的关键):
   - **VDM 回归**: 用新 fixture(mass=145000/L=45)跑现有 test_vessel_dynamics_model,断言物理合理性(Izz ≈ (1/12)·145000·45² ≈ 2.45e7,随 mass·L² 增长;加速度量级合理)。证明 VDM 测试用新参数仍符合物理,非静默漂移。
   - **NomotoFallback 回归**: 用 T_s=6.0 跑现有 test_nomoto_fallback,断言活路径(r₀=0)轨迹仍为纯平移(x+=u·cosψ·dt,y+=u·sinψ·dt,r≡0),**与 T_s=15 输出一致**。证明 T 改值不影响活输出。
   - **fixture 一致性**: 断言 fixture 加载值 == 生产 yaml 加载值(防 fixture 漂移)。

### 回归测试
- 现有 manifest 消费者(MidMpcNode/BC-MPC)加载新 manifest 不报错(编译+启动)
- 现有 test_vessel_dynamics_model / test_nomoto_fallback 全绿(用新 fixture)
- 无 ROS2 消息字段变化(探索已确认,回归验证)
- `test_vessel_dynamics_model.cpp` 全绿(VDM 未改动,P0 不触碰)

### 验收边界(P0 自闭环门)
- [ ] manifest 加载后所有几何/Nomoto 字段值与本 spec 一致(length 45/beam 8.0/draft 1.55/mass 145000/T_s 6.0/K_s 0.3)
- [ ] 解析单测断言 **解析值 == yaml 写入值**(非默认值,捕获静默回退)
- [ ] 6 文件成对更新(yaml key + loader key + hpp 字段 + nomoto_fallback 成员 + fixture),无遗漏
- [ ] VDM 回归:test_vessel_dynamics_model 用新 fixture(mass=145000/L=45)全绿 + 物理合理(Izz 随 mass·L²)
- [ ] NomotoFallback 回归:test_nomoto_fallback 用 T_s=6.0 活路径(r₀=0)轨迹纯平移,与 T_s=15 输出一致
- [ ] fixture 一致性:fixture 加载值 == 生产 yaml 加载值
- [ ] 现有 manifest 消费者(MidMpcNode/BC-MPC)加载新 manifest 编译+启动不报错
- [ ] 无 ROS2 消息字段变化
- [ ] 编译通过(6 文件改动无破坏)

## 风险

- **低**(纯配置 + behavior-preserving 重命名;K 值未参与运算)
- 残留:T_s/K_s 均为 [R22] 数量级估算(2x 误差),标 [TBD-HAZID],P2/海试校准 —— 非阻塞,P0 先落地合理初值
- VDM 推 P2:P0 不触碰 VDM/propagate_own,无结构风险

## 出 P0 范围(后续子项目)

- **P2**: VDM 删除 + propagate_own 处置 + Nomoto 接入 NLP 预测 + T',K' 无量纲重构(若决定转)
- **海试/HAZID**: T_s/K_s 真实辨识(zigzag 试航,IMO MSC.137(76) 框架,FCB<100m 非强制但参考)

## 关联

- 方案包组件 2(TS-12 预测模型):本 P0 落地 manifest 侧,P2 落地 NLP 预测侧
- 决策树日志 TBD-5(VR-TBD5):本 P0 完成字段语义澄清 + 初值;海试校准为残余待办
