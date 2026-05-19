# ENC 电子海图技术实现方案与工程架构 — 面向 FCB 自主航行 TDL（L3）的工程落地建议

## TL;DR
- **核心结论**：当前 GDB 海图方案不可持续。建议采用 **"GDAL/OGR 解析 + 自研 SENC 中间格式（FlatGeobuf+Parquet）+ PostGIS 权威库与 TDL 进程内 boost R-tree 双层索引 + MapLibre GL JS+PMTiles（HMI）+ S-102 HDF5 栅格代价图（算法）"** 的开源混合主线；在 S-63 加密 与 CCS Nx/A2 + DNV AROS 认证路径上引入 **SevenCs Nautilus Maritime Kernel** 或 **Seall ENC Kernel** 作为商业兜底；预计 **24–32 人月**到首版可演示、可认证。
- **认证路径**：CCS《智能船舶规范》(2024)、(2025) 已对智能航行 N、自主操作 A1–A3 标志的 ENC 子系统提出明确功能要求（场景感知必须包含"电子海图数据及更新"，并新增"搁浅预警""碰撞预警""视觉增强""综合信息显示"）；DNV 走 **AROS 标志（DNV-CG-0264，2024 年 12 月新版）+ DNV-RU-SHIP Pt.6 Ch.3 ECDIS 条款（§6.6 防搁浅 / §6.11 NAUT(AW) Track Control）+ 环境测试 DNV-CG-0339 (2021-08)**，并必须通过 **IHO S-64 Ed.3.0.3** 测试数据集。**用户问题中的"TA-0340"不是真实文档号**，正确引用是 DNV-RU-SHIP Pt.6 Ch.3 + 申请表 form code **TA 251**。
- **关键风险与红线**：① **GDAL 至今不支持 S-101 解析**（OSGeo/gdal Issue #13867 仍为 Feature Request 状态，核心障碍是 ISO/IEC 8211 并联重复子字段编码），S-101 必须走商业 SDK；② **S-63 OEM 注册**涉及向 IHO 申请 M_ID 并领取 5 字节 M_KEY，独立安全研究（Heath Henley, 2024-04-24）已公开演示 16⁵=1,048,576 个 hex 字符的密钥空间可被简单暴力枚举（结合 padding 校验），IHO 已通过 **S-100 Part 15 AES-128/192/256 CBC**（密钥长度可选）取代该方案；③ **IACS UR E26/E27 自 2024-07-01 起对新造船合同强制**（Rev.1 版本生效，原 2024-01-01 版本被撤回），ENC 服务作为 CBS（Computer Based System）须满足 IEC 62443-3-3 安全能力。

---

## Key Findings

### 1. 开源工具链可以满足 80% 的 S-57/HMI 需求，但 S-101 与 S-63 是缺口

- **GDAL S-57 驱动**：通过 open options `RETURN_PRIMITIVES=ON, RETURN_LINKAGES=ON, LNAM_REFS=ON` 三件套可拿到 `IsolatedNode/ConnectedNode/Edge/Face` 四类几何基元层，并经 `NAME_RCID_0/1` 与 `RCID` 字段重建对象-基元拓扑关系（gdal.org/drivers/vector/s57.html）。`SPLIT_MULTIPOINT=ON, ADD_SOUNDG_DEPTH=ON` 把 SOUNDG 多点拆为带 `DEPTH` 属性的单点要素，便于直接灌入 PostGIS 点表。注意：POSACC/QUAPOS 不是 COALNE 类属性而是 `M_ACCY/M_QUAL/M_SREL` 属性，需通过 NAME_RCNM/NAME_RCID 共享基元的 layer algebra 连接（Even Rouault 在 gdal-dev mailing list 给出标准做法）。
- **GDAL 暂不支持 S-101**：核心障碍是 S-101 的 ISO/IEC 8211 编码使用并联重复子字段，GDAL 当前 iso8211 解析器无法识别（gdal-dev archive narkive；OSGeo/gdal #13867）。如船型海域涉及 S-101（韩国、日本港口 2025 年起下发测试集），只能依赖 SevenCs Nautilus / Seall ENC Kernel / SevenCs S-101 Reader for FME。
- **S-102 高分辨率水深网格**：**GDAL S-102 驱动自 3.8 版加入**（"Added in version 3.8"，gdal.org/drivers/raster/s102.html；OSGeo 3.8.0 release notes："Add S102 raster read-only driver for S-102 bathymetric products (depends on libhdf5)"），3.12 增加多 feature-instance-group 支持，3.13 增加写支持。Band 1 = 深度（正值代表垂直基准下方），Band 2 = 不确定度；`DEPTH_OR_ELEVATION=ELEVATION` 翻转符号。支持 S-102 v2.1/v2.2/v3.0。**NOAA 已通过 AWS Open Data**（`noaa-s102-pds` S3 bucket，DCF2 Regular Grid，按 NOS National Bathymetric Source 区域组织）公开发布 v3.0 数据。S-100 Python 操作推荐 **`s100py`**（s100py.readthedocs.io，NOAA Barry Gallagher/Glen Rice 维护）。
- **S-52 渲染**：完整开源实现以 **OpenCPN 的 `s52plib`**（GPLv2，C++，OpenGL/OpenGL ES）和 sduclos/S52（`libS52.so`，基于 PresLib Ed. 3.2）为代表。当前 IHO 官方 PresLib 已到 **Edition 4.0/4.0.2（2014-10）**，须从 IHO 获取（非免费公开）。开源实现的局限：OpenCPN s52plib 对水下危险品过程做了简化，默认假设 30 m safety depth；SMAC-M（GitHub LarsSchy）的 MapServer 端口存在 TOPMAR/DEPCNT 等限制。
- **空间数据库与索引**：PostGIS（GIST + R-Tree）一次"自然地球 admin0 258 多边形 vs populated places 7342 点"的空间 join，无索引耗时 2200 ms，**有索引 200 ms（10× 加速）**（Crunchy Data 官方 benchmark）；低重叠数据上 SP-GIST 可再快 5–15%。**对 TDL 毫秒级查询的硬约束**：PostGIS 网络往返 1–2 ms 已是底线，决策层必须在 TDL 进程内维护内存镜像（`boost::geometry::index::rtree` 或 libspatialindex），PostGIS 仅作权威源。
- **瓦片管线**：MapLibre GL JS + PMTiles 已成为新一代默认方案 —— PMTiles 是"single-file format for hosting tilesets without a server or API, just S3 or other storage"（protomaps.com），pmtiles JS 库通过 `maplibregl.addProtocol('pmtiles', protocol.tile)` 注入；TileServer GL（maptiler/tileserver-gl）提供 MapLibre GL Native 服务端栅格化。船端离线推荐组合：`ogr2ogr → tippecanoe → .pmtiles → MapLibre GL JS`。

### 2. 商业 SDK：四选一，并非可替代

| SDK | 标准支持 | 关键特征 | 适合度 |
|---|---|---|---|
| **SevenCs Nautilus Maritime Kernel** | S-100/S-101 读写、S-57、S-52、S-63、S-64、IEC 61174 Ed.4 / IEC 62288 Ed.3、AML 3.0/2.1、bENC、AIO、STANAG 7170 | C/C++/C# (SharpCoat)、Windows+Linux 32/64-bit、Wibu CodeMeter 授权、官方称"more than 40,000 licenses sold worldwide" | **认证首选**：CCS/DNV 都接受其 IEC 61174 一致性 |
| **Seall ENC Kernel** | S-57、S-101、S-63、S-100 Data Protection、S-52 PresLib + **S-100 Portrayal Framework** | 模块化（Core + Route + Weather），面向 USV/仿真/态势感知 | 中等：S-100 portrayal 是杀手锏 |
| **浩海 ChartBox SDK**（haohaidata.com） | S-57、S-100，跨平台（含 HarmonyOS 适配） | 国内自主，Web/Desktop/Mobile 全栈，提供云 API（海图/AIS/航线/气象） | 国内备份，CCS 沟通成本最低 |
| **舟山 sailxy(航易)/zshaitu** | S-57/S-52，C++ 跨平台（Windows/Android/iOS/Linux/VxWorks/WinCE） | 二次开发友好，私有云部署 | 价格友好，缺 S-101/S-63 |

**SevenCs ChartHandler** 还独立提供 type-approved ENC import、S-63 解密、S-64 测试集导入；ENC Encryptor Package 用于自建数据分发（S-63 加密 + Permit Generator）。**Luciad / Hexagon LuciadLightspeed** 提供另一企业级路径（详尽的 S-63 文档 + S-101 支持，dev.luciad.com），适合岸基/服务器侧场景。

### 3. S-63 加密：M_KEY/HW_ID 是商业 vs 自研的真分水岭

机制（Luciad 官方文档 + IHO S-63 Guidance Notes + Heath Henley 2024-04-24 blog 分析交叉验证）：
- 制造商有唯一 5 字节 hex **M_KEY**；为每个 EPS（ENC Processing System）选定 5 字节 **HW_ID**；用 M_KEY + Blowfish ECB 加密 HW_ID，再附 CRC32 与 4 位 M_ID 拼成 **28 位 User Permit**；数据商用其所掌握的 M_KEY 列表反解 HW_ID，再用 HW_ID 加密 cell key 下发 `PERMIT.TXT`。
- **自研法律门槛**：要在 IHO 注册 M_ID 并领取 M_KEY，进入 IHO S-63 Scheme Administrator 的 OEM 注册流程；如果只做 ECS（非 type-approved ECDIS），可以购买已许可制造商（SevenCs、Luciad）的 license + 自有 HW_ID 注入接口（如 `TLcdS63UnifiedModelDecoder#setEncryptedHardwareID`）。
- **公开已知缺陷**：Heath Henley（heathhenley.dev，2024-04-24）演示 5 字节 hex 字符集 M_KEY 的密钥空间仅 **16⁵ = 1,048,576** 个组合，可通过暴力枚举配合 padding 字节检查（"valid padding" + "3 bytes of padding"）在普通桌面上短时间内枚举出 M_KEY 与 HW_ID。这是 IHO 推动 **S-100 Part 15 AES（CBC，128/192/256 bit 密钥可选）** 取代 S-63 的根本原因（Heath Henley 原文："AES in CBC block mode is used for encryption instead of Blowfish... the key size can be 128, 192, or 256 bits"）。

### 4. 法规：CCS 与 DNV 两条路径要点

**CCS《智能船舶规范》2024（2024-04-01 生效）/ 2025（2025-04-01 生效）**：
- §2.2.4 智能航行场景感知必选输入第 (4) 项明确为"**电子海图数据及更新**"。
- §2.3.3 要求"按《1972 年国际海上避碰规则》要求实施避碰决策和操作"。
- §3 设备配备列表第 ④ 项为 ECDIS；§3.1.3 要求"航路航速设计和优化系统应符合 **II 类计算机系统**的要求"（CCS 钢质海船入级规范 Pt.7）。
- 2024 版**新增**："航路与航速设计和优化、视觉增强、**碰撞预警**、**搁浅预警**、综合信息显示等功能要求和技术要求"。
- 标志体系：**N**（智能航行基础）、**Nx/Tx**（扩展/拖轮）、**R1/R2**（远程，船上有/无人员）、**A1/A2/A3**（自主递进）；FCB-L3 战术层对应 **A2** 或 **N + Nx 组合**。
- **CCS《无人水面艇检验指南》2024**（替代 2018 版）分远程控制 / 部分自主 / 全程自主三模式，新增计算机系统、网络安全、电磁兼容、数据存储要求。
- 中国 MSA《无人艇技术与检验暂行规则（征求意见稿）》于 2024 年完成公示，将成为统计性配套。

**DNV 路径**：
- ① 传统 ECDIS Type Approval：**DNV-RU-SHIP Pt.6 Ch.3**（"Navigation, manoeuvring and position keeping"，Ed. July 2019 amended Oct 2020）的 §6.6 防搁浅决策支持、§6.8 BNWAS、§6.9 Bridge Alert Management、§6.11 NAUT(AW) Track Control；按 **TA 251** 申请表提交。
- ② 叠加 **AROS 标志（DNV-CG-0264 "Autonomous and remotely operated vessels"，2024-12 新版）**，覆盖 remote control / decision support / supervised autonomy / full autonomy 四模式；引用 **IMO MSC.1/Circ.1455** 做 flag-state 等效性评估。第一个 Statement of Compliance 发给 Ocean Infinity Armada 78 03。
- 环境测试：**DNV-CG-0339 (2021-08)** —— 温度 / 湿度 / 振动 / EMC / 防护等级。
- 强制引用：**IEC 61174 Ed.4、IHO S-52 Ed.6.1 + PresLib 4.0、S-63 Ed.1.2、S-64 Ed.3.0**（Highlander DNV TA 281 形式可见样例）。
- **S-100 ECDIS（IEC 61174 Ed.5、IMO MSC.530(106)/Rev.1）**：IEC 61174 Ed.5 **预计 2027 年 7 月发布**（SAFETY4SEA 引 IEC TC80 work programme），EU MED Wheelmark 通常滞后 6–12 个月，**最早 2027 年底至 2028 年初可得**。**UKHO ADMIRALTY 明确**："From 1 January 2029: All newly installed ECDIS must meet the new standard (MSC.530(106)/REV.1) — meaning they must be S-100 compatible. This also applies to retrofits."

**网络安全 — IACS UR E26 + E27**：**Rev.1 版本于 2024-07-01 对新造船合同强制**（IACS press release："Only the Rev.1 versions will enter into force and the entry in force date will be July 1, 2024"），ENC 服务层作为 CBS 需满足 IEC 62443-3-3 选定安全能力（资产清单、物理+逻辑拓扑图、用户认证、审计日志、传输完整性加密，以及 E26 提到的 5 大功能：识别、保护、检测、响应、恢复）。

### 5. 代价地图与 FMM

- **Fast Marching Square (FM²)**：Garrido et al., *Frontiers in Robotics and AI*, 2020, doi:10.3389/frobt.2020.00002 系统对比 FMM/FMVF/FM²/RRT*。FM² 输出 funnel-shaped time-of-arrival map，**适合"避碰完成后回归主航线"场景**：在原规划航路 + 安全缓冲区做局部 FMM 即满足"安全区域内仅重新规划"；超出缓冲区时全局 FMM 验证新航路无搁浅风险。**关键坑**：FM² 默认贴障碍走最短路 —— 必须先对二值障碍图做距离场/高斯卷积形成 speed map 再 FMM，否则会贴岸航行。开源实现 **scikit-fmm**（PyPI，C++/Python，scikit-fmm/scikit-fmm GitHub）。
- **CATZOC → 安全阈值映射表**（IHO S-67 Mariner's Guide）：A1 = 0.5 m + 1%·d，位置 ±5 m + 5%·d；A2 = 1.0 m + 2%·d，位置 ±20 m；B = 1.0 m + 2%·d，位置 ±50 m；C = 2.0 m + 5%·d，位置 ±500 m；D 更差；U 未评估。**TDL 应将 CATZOC C 以下区域自动按 +500 m 横向 buffer + 5% 深度增量加固为 MPC 硬约束**。
- **动态 UKC = Static UKC – Squat – Heel – Heave/Roll allowance**。Squat 经验公式：**Barrass**: S = Cb·V²/100（开阔水）、S = Cb·V²·2.65/100（受限运河）；PIANC 推荐：**Yoshimura**（开阔水/概念）、**Römisch**（受限/详细）、**Huuska**（详细）。学术对比（Briggs, PIANC）显示 Barrass 在 V > 6.54 kn 时偏低且不跟随实测增长；FCB 船速高（典型 20–25 kn），建议同时计算 **Barrass + Römisch 取较大值**。

---

## Details: 推荐分层架构

```
┌─────────────────────────────────────────────────────────────┐
│  L6 — Data Quality Gate（数据质量门控层）                    │
│  • S-58/S-64 校验 • CATZOC 自动评级 • WGS84 强制断言         │
│  • 非 HO 数据红色横幅 • POSACC > 50 m 阻拦                   │
└──────┬──────────────────────────────────────────────────────┘
       │
┌──────┴───────────┐   ┌────────────────────────────────────┐
│ L1 摄入层         │──▶│  L2 解析/转换层（SENC Builder）     │
│ S-57 .000(GDAL)  │   │  GDAL(S-57) + SevenCs(S-101)       │
│ S-101 GML(商业)   │   │  → FlatGeobuf(几何) + Parquet(属性)│
│ GDB / GeoTIFF /  │   │  分层：DEPARE/DEPCNT/OBSTRN/WRECKS │
│ S-102 HDF5        │   │  /COALNE/LIGHTS/TSSLPT/...         │
└──────────────────┘   └─────────┬──────────────────────────┘
                                  │
                ┌─────────────────┼───────────────────────────┐
                │                 │                           │
        ┌───────▼────────┐ ┌──────▼─────────┐ ┌──────────────▼────────────┐
        │ L3 空间索引层   │ │ L4 代价地图    │ │ L5 HMI 渲染层              │
        │ PostGIS 16+    │ │ S-102→GDAL→   │ │ Web: MapLibre GL + PMTiles │
        │ PG3.4 GiST权威 │ │ NumPy → FM²    │ │ Desktop: Qt + s52plib(C++)│
        │ TDL内存R-tree  │ │ MPC 硬约束输出│ │ S-52 PresLib Ed.4.0        │
        │ <1 ms p99 点查 │ │ scikit-fmm     │ │ S-101 时切 Nautilus Kernel│
        └────────────────┘ └────────────────┘ └────────────────────────────┘
```

**L1 摄入层**：FME（SevenCs S-101 Reader/Writer for FME）或自研 Python 摄入器。GDB 旧数据通过 `ogr2ogr -f FlatGeobuf` 一次性导出后退役。

**L2 SENC 中间格式**：**FlatGeobuf**（GDAL 原生，按要素流式读取，内置 spatial index header）+ **Parquet**（属性表）混合方案。每个 ENC cell 解出一组分层 FlatGeobuf，并保留 `M_QUAL`/`M_ACCY` 多边形作为质量遮罩。

**L3 空间索引层（双层）**：
- **PostGIS 权威库**（PostgreSQL 16 + PostGIS 3.4，GiST R-Tree）：ETL、版本管理、多船共享、岸基服务。索引建立必须 `CREATE INDEX ... USING GIST (geom)`，否则退化为 B-tree 无加速（PostGIS FAQ 明确警告）。
- **TDL 进程内内存索引**：`boost::geometry::index::rtree<value, quadratic<16>>`，启动时从 PostGIS 拉取当前 ENC cell 的 OBSTRN/COALNE/DEPARE/DEPCNT 全集，常驻 RAM；目标 `FindPointInAnyBoundary` **p99 < 1 ms**。

**L4 代价地图服务**：
- 静态障碍层：OBSTRN/UWTROC/WRECKS/COALNE 栅格化 → `binary_obstacle.tif`（10 m 分辨率，按 ENC cell 区块）。
- 深度层：S-102 HDF5 → GDAL Band 1 → `safe_depth = depth - dynamic_draft - squat(Barrass∨Römisch) - heel - swell`。
- CATZOC 层：M_QUAL 多边形 → CATZOC 等级栅格 → 安全余量增量。
- FM² 速度场：`speed = sigmoid(safe_depth - threshold)` 并对静态障碍做距离场卷积；scikit-fmm 计算 time-of-arrival，gradient descent 抽出航路。

**L5 HMI 渲染层**：
- **Web HMI（首选）**：MapLibre GL JS + PMTiles 协议（船端打包 + 增量 NM 更新），自定义 S-52 风格表（OpenCPN chartsymbols.xml 迁移到 MapLibre style spec）。Tile 生成：`tippecanoe -o enc.pmtiles -z 14 -Z 4 *.fgb`。
- **桌面备份**：Qt 6 + 嵌入 OpenCPN s52plib（注意 GPLv2 传染性，须 GPL 合规审查；或购 SevenCs Nautilus C++ API 规避）。

**L6 数据质量门控**：
- 启动时跑 **S-64 Ed.3.0.3** 测试集自检（6 类测试：Loading / Updating / Display / Permits / Encryption / Multi-supplier）。
- 规则引擎：CATZOC < B 区域穿越告警；POSACC > 50 m 不允许进入安全契约；非官方源（无 IHO Producer Code）显式 "NON-HO DATA" 红色横幅。

---

## Recommendations

1. **立即（M0–M1，4 个月内）**：冻结 GDB 路径，完成 PostGIS + MapLibre + TDL R-tree 内存索引联通 demo。**门槛**：TDL 点查 p99 < 1 ms；HMI 加载 ≥ 1 个 ENC cell 区域所有图层可勾选展示；至少 5 类质量校验告警可触发。
2. **3 个月内**：申请 SevenCs Nautilus Kernel 评估许可（free evaluation license 可申）以备 S-101 切换。**触发升级条件**：航行海域出现 S-101 数据（任何 IHO 成员国发布）即切换。
3. **6 个月内（M2–M3）**：跑通 **IHO S-64 Ed.3.0.3** 测试集（IHO 公开 PDF，自行下载执行）；S-102 + S-104（潮汐）+ Barrass squat 串通；FM² 回归主航线 demo 验收（缓冲区内规划 < 200 ms，超界全局规划 < 2 s）。**门槛**：S-64 六大类测试 100% pass。
4. **9 个月内（M4）**：联系 **CCS 规范与技术中心（上海规范研究所）** 和 **DNV Trondheim Cyber Lab** 做预沟通。**对接文件**：系统架构、S-64 测试报告、IACS E26 资产清单与拓扑图、E27 IEC 62443-3-3 安全能力对照表。
5. **12 个月内（M5）**：完成 **DNV-CG-0339 (2021-08)** 环境测试外送（温度/湿度/振动/EMC/防护等级）；CCS 智能船舶预审。
6. **15 个月内（M6）**：正式申报 **CCS 智能船舶 Nx + A2 标志** 和 **DNV AROS Statement of Compliance**。

**回退到全商业 SDK 的触发条件（任一即立即启动）**：① 认证机构明确反馈自研 S-52 PresLib 实现不被接受；② S-101 数据进入主作业海域；③ S-63 OEM 注册流程被 IHO 拒绝（少见，商业 EPS 通常托管在已注册 manufacturer 名下解决）。

---

## Caveats

- **S-101/S-100 时间窗口**：IEC 61174 Ed.5 **预计 2027 年 7 月发布**（IEC TC80 work programme，SAFETY4SEA 引述），EU MED Wheelmark 滞后 6–12 个月，最早 2027 年底至 2028 年初；**2029-01-01 起所有新装 ECDIS 须支持 S-100（含改装）**（UKHO ADMIRALTY 明确）。本架构对 S-101 的支持目前只能通过商业 SDK 兜底，至少观望到 2027 年 GDAL 是否解决 iso8211 解析问题（GDAL Issue #13867）。
- **OpenCPN s52plib 是 GPLv2**：直接静态/动态链接会传染整个 HMI 模块；若不接受 GPLv2，须改用 Nautilus / Seall Kernel，或自实现 S-52 PresLib（开发量约 6 人月，且 PresLib Ed.4.0 字典须向 IHO 付费获取）。
- **PostGIS 不能直接服务 TDL 高频查询**：网络往返 + 解析序列化已是 1–2 ms 下限。**必须**在 TDL 进程内维护内存 R-tree 镜像；PostGIS 仅做版本权威源、ETL 落地、岸基服务。
- **S-63 即将被 S-100 Part 15 取代**：当前 5 字节 hex M_KEY 密钥空间仅 16⁵≈10⁶，已被独立安全研究公开演示可被暴力枚举（Heath Henley, heathhenley.dev/posts/hacking-on-s63/, 2024-04-24）；**S-100 Part 15 改用 AES-CBC，密钥长度可选 128/192/256 位**。如新建系统寿命 ≥ 10 年，应同时设计 S-100 Part 15 数据保护通道。
- **"DNV TA-0340" 不是真实文档号**：正确引用为 **DNV-RU-SHIP Pt.6 Ch.3 + 申请表 form code TA 251 + 环境测试 DNV-CG-0339 + AROS 指南 DNV-CG-0264 (2024-12)**。后续内部文档须修正。
- **CCS 标志命名约定**：CCS 使用 N/Nx/A1-A3/R1-R2，**未发现 "Nn" 作为独立官方标志**。如内部继续使用 "Nn"，须在与 CCS 沟通时确认对应到 **Nx**（智能航行扩展）或 **A2**（中等程度自主）。
- **FM² 单独使用不够**：在贴岸狭水道默认会贴岸走最短路；务必先对静态障碍二值图做距离场/高斯卷积形成 speed map 再 FMM。Garrido et al. 2020 也明确"any FMM-like method may be used to implement these applications"，但 vector-field FMVF 在洋流/风场环境更优。
- **CATZOC 不覆盖动态地形**：沙波区/泥沙底/季节性变化即使 CATZOC A1 也存在沉积变化（ADMIRALTY/UKHO Tom Mellor 提醒）；TDL 应订阅 NM/NtM 航海通告增量更新并定期重做安全契约。
- **IACS UR E26 原 2024-01-01 版本已被撤回**：仅 Rev.1 于 2024-07-01 生效（IACS 官方 press release "Only the Rev.1 versions will enter into force and the entry in force date will be July 1, 2024"），引用文档时须注意版本。

---

## 完成度对照表

| 计划项 | 覆盖位置 |
|---|---|
| 1. GDAL S-57 RETURN_PRIMITIVES API | Key Findings #1 第 1 项 |
| 2. S-102 HDF5 处理 | Key Findings #1 第 3 项 + L4 |
| 3. PostGIS vs SpatiaLite vs R-tree | Key Findings #1 第 5 项 + L3 |
| 4. 瓦片生成 MapTiler/QGIS/GeoServer | Key Findings #1 第 6 项 + L5 |
| 5. MapLibre/Leaflet/OpenLayers + Qt | L5 |
| 6. SevenCs Nautilus 商业 SDK | Key Findings #2 表 |
| 7. NAVTOR/Wärtsilä/国内 SDK | Key Findings #2 表（浩海/sailxy/Seall）|
| 8. CCS 智能船舶规范 ENC 要求 | Key Findings #4 CCS 段 |
| 9. DNV ECDIS TA-0340 / TA 251 | Key Findings #4 DNV 段（已修正）|
| 10. IHO S-64 测试集 | Key Findings #4 + L6 + Recommendation 3 |
| 11. IACS UR E26/E27 | TL;DR + Key Findings #4 + Caveats |
| 12. S-63 M_KEY/HW_ID 商业 vs 自研 | Key Findings #3 |
| 13. 混合 S-57/S-101 适配层 | L1+L2 |
| 14. CATZOC 自动检查实现 | Key Findings #5 + L6 |
| 15. WGS84 坐标统一 | L6 |
| 16. 推荐分层架构 | Details 章节 |
| 17. 迁移路径 | M0–M6 表 |
| 18. 人月估算 | TL;DR + M0–M6 表（24–32 人月）|
| 19. 认证对接点 | Recommendations 4–6 |