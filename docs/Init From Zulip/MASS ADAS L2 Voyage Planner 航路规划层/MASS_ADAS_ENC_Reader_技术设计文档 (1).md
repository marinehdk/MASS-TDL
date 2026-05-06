# MASS_ADAS ENC Reader 电子海图解析器技术设计文档

**文档编号：** SANGO-ADAS-ENC-001  
**版本：** 0.1 草案  
**日期：** 2026-04-25  
**编制：** 系统架构组  
**状态：** 开发参考

---

## 目录

1. 概述与职责定义
2. 输入与输出接口
3. ENC 数据格式与要素编码
4. 软件架构：四层模型
5. 第一层：数据解析层（Parser）
6. 第二层：空间索引层（Spatial Index）
7. 第三层：语义解释层（Semantic Interpreter）
8. 第四层：查询服务层（Query Service）
9. 数据完整性与质量检查
10. 安全等深线自动选取
11. 特殊要素处理
12. 海图拼接与多比例尺管理
13. 性能要求与优化策略
14. ROS2 接口设计
15. 与其他模块的协作关系
16. 实现建议与开发路线
17. 测试策略与验收标准
18. 参考文献与标准

---

## 1. 概述与职责定义

### 1.1 模块定位

ENC Reader（电子海图解析器）是 MASS_ADAS 五层架构中 Layer 2（Voyage Planner，航路规划层）的基础数据子模块。它是整个航路规划系统的"眼睛"——船长审图时看到的一切信息，都必须通过这个模块被系统"看到"并理解。

### 1.2 核心职责

ENC Reader 的职责可以概括为四个词：**解析、索引、解释、服务。**

- **解析：** 将 IHO S-57 格式的电子海图文件解析为内存中的结构化数据对象。
- **索引：** 对解析后的海图要素建立高效的空间索引，支持毫秒级地理查询。
- **解释：** 将原始海图要素转化为对航线规划有直接意义的语义信息——不仅告诉系统"这里有什么"，还告诉系统"这对我的船意味着什么"。
- **服务：** 向 WP Generator、Speed Profiler、Safety Checker 等上游模块提供标准化的查询接口。

### 1.3 设计原则

- **保守解释原则：** 当海图数据存在模糊性时（如水深未知的碍航物），始终取最保守的解释。
- **数据不信任原则：** 对海图数据的时效性和精度保持警惕，通过 ZOC 等级和更新日期进行质量评估，并在输出中附带数据置信度信息。
- **性能优先原则：** 航线规划阶段可能调用 ENC Reader 数百至数千次。单次查询延迟必须控制在毫秒级。
- **可扩展原则：** 首先支持 S-57 格式，架构上预留 S-100/S-101 扩展接口。

---

## 2. 输入与输出接口

### 2.1 输入

| 输入项 | 来源 | 格式 | 说明 |
|-------|------|------|------|
| ENC 海图文件 | 水文局发行 / 海图数据供应商 | IHO S-57（.000 文件） | 可能包含多幅不同比例尺的海图 |
| ENC 增量更新文件 | 水文局每周发布 | S-57 增量（.001, .002 等） | 航行中接收的海图更新 |
| 本船参数 | Parameter DB | — | 吃水 T、船宽 B、最高点离水高度（Air Draft）|
| UKC 标准 | Parameter DB / 配置文件 | — | 各水域类型的最小 UKC 要求 |
| 潮汐预报数据 | Weather Routing / 外部服务 | — | 可选，用于动态水深修正 |

### 2.2 输出

ENC Reader 通过函数调用和 ROS2 Service 对外提供六类查询结果（详见第 8 节），同时在初始化完成后发布一次性的海图元信息摘要。

---

## 3. ENC 数据格式与要素编码

### 3.1 S-57 数据模型概述

S-57（IHO Transfer Standard for Digital Hydrographic Data）的数据模型由三个核心概念组成：

- **要素（Feature）：** 海图上的一个地理实体，如一块水深区域、一座灯塔、一条等深线。每个要素有唯一的六字符要素代码（Feature Code）。
- **属性（Attribute）：** 描述要素特征的键值对。如水深值（VALSOU）、灯质（LITCHR）、限制类型（RESTRN）。每个属性有唯一的六字符属性代码。
- **空间对象（Spatial Object）：** 要素的地理位置和形状，可以是点（Point）、线（Line）或面（Area）。

S-57 文件的物理编码基于 ISO 8211 标准，是一种二进制格式。

### 3.2 航线规划相关的核心要素编码

以下按船长审图的优先级顺序排列，分为七大类。

**第一类：水深信息要素（最高优先级）**

| 要素代码 | 中文名称 | 几何类型 | 关键属性 | 航线规划意义 |
|---------|---------|---------|---------|------------|
| DEPARE | 水深区域 | 面 | DRVAL1（浅限值）, DRVAL2（深限值） | 定义一个水深范围区域，如"5m~10m 水深区" |
| DEPCNT | 等深线 | 线 | VALDCO（等深线值） | 标注特定水深的等值线 |
| SOUNDG | 测深点 | 点（多点） | EXPSOU（暴露状况）, QUASOU（质量） | 单个位置的实测水深值（存储在几何坐标的 Z 分量中） |
| DRGARE | 疏浚区域 | 面 | DRVAL1（维护水深）, DRVAL2, SORDAT（测量日期） | 人工维护的航道深度，可能因淤积而变浅 |

**第二类：碍航物要素**

| 要素代码 | 中文名称 | 几何类型 | 关键属性 | 航线规划意义 |
|---------|---------|---------|---------|------------|
| OBSTRN | 碍航物 | 点/线/面 | VALSOU（最浅水深）, CATOBS（类别）, WATLEV（水位关系） | 通用碍航物（礁石、桩基、渔具等） |
| WRECKS | 沉船 | 点/面 | VALSOU, CATWRK（沉船类别）, WATLEV | 沉船位置和最浅水深 |
| UWTROC | 水下暗礁 | 点 | VALSOU, WATLEV | 水下岩石，可能仅在低潮时暴露 |
| LNDARE | 陆地区域 | 面 | — | 绝对不可进入的区域 |
| SLCONS | 岸线建筑 | 线/面 | — | 海堤、防波堤等岸线人工建筑 |

**第三类：航行限制区域要素**

| 要素代码 | 中文名称 | 几何类型 | 关键属性 | 航线规划意义 |
|---------|---------|---------|---------|------------|
| RESARE | 限制区域 | 面 | RESTRN（限制类型）, CATREA（区域类别）, TXTDSC（说明文本） | 各类航行限制区域 |
| MIPARE | 军事演习区 | 面 | CATMPA（类别）, PERSTA/PEREND（生效期间） | 可能有临时或永久航行限制 |
| CTNARE | 谨慎区域 | 面 | — | 需要特别谨慎航行的区域 |
| ISTZNE | 沿岸通航带 | 面 | — | TSS 外侧的沿岸通航带 |

**RESARE 的 RESTRN 属性值详解：**

| RESTRN 值 | 含义 | 对航线规划的影响 |
|-----------|------|----------------|
| 1 | 锚泊禁止 | 不影响航行，仅限制锚泊 |
| 2 | 捕鱼禁止 | 不影响航行 |
| 3 | 航行禁止 | **必须完全避开** |
| 4 | 排污禁止 | 不影响航行 |
| 5 | 拖网禁止 | 不影响航行 |
| 6 | 停泊禁止 | 不影响航行 |
| 7 | 进入禁止 | **必须完全避开** |
| 8 | 停留禁止 | 可以通过但不可停留 |
| 9 | 施工区域 | **应避开或按通告要求通过** |
| 13 | 潜水作业区 | **应避开或降速通过** |
| 14 | 速度限制 | **应用限速值** |
| 27 | 有条件进入 | 需检查具体条件 |

**第四类：交通管理设施要素**

| 要素代码 | 中文名称 | 几何类型 | 关键属性 | 航线规划意义 |
|---------|---------|---------|---------|------------|
| TSSLPT | TSS 通航分道 | 面 | ORIENT（规定航向） | 规定方向的通航分道 |
| TSSBND | TSS 边界 | 线 | — | 分隔带边界线 |
| TSSRON | TSS 环形交通区 | 面 | — | 环形交通区域 |
| TSEZNE | TSS 两方通航区 | 面 | — | 可双向通航的区域 |
| PRCARE | 警戒区 | 面 | — | TSS 交汇处的警戒区域 |
| RCRTCL | 推荐航路 | 线 | ORIENT, TRAFIC（单向/双向） | 推荐但非强制的航行方向 |
| DWRTPT | 深水航路 | 面 | DRVAL1（最小水深）, ORIENT, TRAFIC | 为深吃水船舶设计的航路 |
| FAIRWY | 推荐航道 | 面 | ORIENT, TRAFIC | 推荐使用的航道 |

**第五类：桥梁与架空设施要素**

| 要素代码 | 中文名称 | 几何类型 | 关键属性 | 航线规划意义 |
|---------|---------|---------|---------|------------|
| BRIDGE | 桥梁 | 点/线 | VERCLR（净空高度）, VERCCL（闭合净空）, VERCOP（开启净空） | 垂直净空限制 |
| CBLOHD | 架空电缆 | 线 | VERCLR（净空高度） | 垂直净空限制 |
| PIPOHD | 架空管线 | 线 | VERCLR（净空高度） | 垂直净空限制 |
| CONVYR | 传送带（架空） | 线 | VERCLR | 垂直净空限制 |

**第六类：助航标志要素**

| 要素代码 | 中文名称 | 几何类型 | 关键属性 |
|---------|---------|---------|---------|
| LIGHTS | 灯光 | 点 | LITCHR（灯质）, SIGPER（周期）, VALNMR（射程）, COLOUR（颜色） |
| BCNLAT | 侧面标 | 点 | COLOUR, CATLAM（类别：左侧/右侧） |
| BOYLAT | 侧面浮标 | 点 | COLOUR, CATLAM |
| BCNCAR | 方位标 | 点 | CATCAM（类别：北/东/南/西） |
| BOYCAR | 方位浮标 | 点 | CATCAM |
| BCNISD | 孤立危险物标 | 点 | — |
| BOYISD | 孤立危险物浮标 | 点 | — |
| BCNSAW | 安全水域标 | 点 | — |
| BOYSAW | 安全水域浮标 | 点 | — |
| RTPBCN | 雷达应答器 | 点 | SIGGRP（信号组） |

**第七类：海岸线与地形要素**

| 要素代码 | 中文名称 | 几何类型 | 关键属性 |
|---------|---------|---------|---------|
| COALNE | 海岸线 | 线 | CATCOA（类别：陡崖/沙滩/泥滩等） |
| LNDARE | 陆地区域 | 面 | — |
| LNDELV | 陆地高程 | 点/线 | ELEVAT（高程值） |
| SLOTOP | 海岸坡顶 | 线 | — |
| ACHARE | 锚地 | 面 | CATACH（类别） |
| PILBOP | 引水站 | 点 | CATPIL（类别） |
| MORFAC | 系泊设施 | 点/线/面 | CATMOR（类别） |

### 3.3 其他重要要素

| 要素代码 | 中文名称 | 说明 |
|---------|---------|------|
| CBLSUB | 海底电缆 | 禁止抛锚区域，航行通常可以 |
| PIPSOL | 海底管线 | 禁止抛锚，可能有浅埋段突出海底 |
| MARCUL | 海洋养殖 | 渔排、网箱区域，应避开 |
| OSPARE | 海上平台安全区 | 石油平台周围 500m 安全区 |
| OFSPLF | 海上平台 | 石油/天然气平台 |
| MAGVAR | 磁差 | 当地磁差值和年变率 |
| TS_FEB | 潮流信息 | 潮流菱形数据 |
| M_QUAL | 数据质量 | ZOC 等级标注区域 |
| M_SREL | 测量可靠性 | 测量方法和日期 |
| M_COVR | 数据覆盖 | 海图覆盖范围元信息 |

---

## 4. 软件架构：四层模型

ENC Reader 内部分为四个层次，每层有明确的职责边界：

```
┌────────────────────────────────────────────────────────┐
│           第四层：查询服务层 (Query Service)              │
│  对外暴露 6 类标准查询接口，供 WP Generator 等调用       │
├────────────────────────────────────────────────────────┤
│          第三层：语义解释层 (Semantic Interpreter)        │
│  将原始要素转化为航线规划语义：危险性分级、限制解析、       │
│  水深安全性判定、水域类型分类                              │
├────────────────────────────────────────────────────────┤
│           第二层：空间索引层 (Spatial Index)              │
│  R-tree 空间索引，支持毫秒级地理范围查询                  │
├────────────────────────────────────────────────────────┤
│            第一层：数据解析层 (Parser)                    │
│  解析 S-57 .000 文件，多海图拼接，增量更新应用             │
└────────────────────────────────────────────────────────┘
              ↑
        S-57 ENC 文件 (.000, .001, .002 ...)
```

---

## 5. 第一层：数据解析层（Parser）

### 5.1 技术选型

推荐使用 GDAL/OGR 库作为 S-57 解析引擎。

| 选项 | 优点 | 缺点 | 推荐度 |
|------|------|------|--------|
| GDAL/OGR（C++ / Python） | 工业级标准库，S-57 驱动成熟；支持多种格式互转；社区活跃 | 库体积较大（约 20MB） | 强烈推荐 |
| libsenc（C） | 轻量级，专为 S-57 设计 | 社区小，维护不确定 | 备选 |
| 自研解析器 | 完全可控 | 开发工作量极大，ISO 8211 解析复杂 | 不推荐 |

**GDAL 安装：**

```bash
# Ubuntu
sudo apt install libgdal-dev gdal-bin python3-gdal

# C++ CMake 集成
find_package(GDAL REQUIRED)
target_link_libraries(enc_reader_node ${GDAL_LIBRARIES})
```

### 5.2 GDAL 关键配置项

S-57 驱动有多个影响解析行为的环境变量和配置选项：

| 配置项 | 建议值 | 说明 |
|-------|--------|------|
| OGR_S57_OPTIONS | "SPLIT_MULTIPOINT=ON,ADD_SOUNDG_DEPTH=ON" | SPLIT_MULTIPOINT：将多点测深拆为单点（便于索引）；ADD_SOUNDG_DEPTH：将水深值从 Z 坐标提升为属性字段 |
| S57_PROFILE | "EN" | 使用 ENC 配置文件（而非非标产品配置） |
| RETURN_PRIMITIVES | "ON" | 返回空间基元数据 |
| RETURN_LINKAGES | "ON" | 返回要素间关联关系 |

### 5.3 解析流程

```
输入：一组 S-57 .000 文件路径

FOR EACH .000 文件:
    1. 使用 GDAL 打开数据源
       dataset = ogr.Open(filepath)
    
    2. 读取数据集标识记录（DSID）
       → 获取比例尺（CSCL）、更新日期（UADT）、版本号（EDTN）
       → 获取水深单位（DUNI）、高度单位（HUNI）、坐标精度（CMFX/CMFY）
    
    3. 遍历所有要素层
       FOR EACH layer IN dataset:
           FOR EACH feature IN layer:
               → 提取要素代码（Feature Code）
               → 提取全部属性（键值对字典）
               → 提取空间几何对象（WKT 或内部 Geometry 对象）
               → 构造 EncFeature 对象
               → 加入全局要素集合
    
    4. 应用增量更新文件（如存在）
       FOR EACH .001, .002 ... 更新文件:
           → 按 S-57 更新指令执行增删改操作

输出：EncFeatureCollection（全局要素集合）
```

### 5.4 内部数据结构

```
struct EncFeature {
    string feature_code;           // 六字符要素代码，如 "DEPARE"
    map<string, Variant> attrs;    // 属性字典，如 {"DRVAL1": 5.0, "DRVAL2": 10.0}
    Geometry geometry;             // 空间几何（点/线/面）
    BoundingBox bbox;              // 最小外接矩形（用于 R-tree 索引）
    
    // 元信息
    string chart_id;               // 所属海图编号
    int chart_scale;               // 所属海图比例尺分母
    string update_date;            // 最后更新日期
    int zoc_level;                 // 数据质量等级（A1/A2/B/C/D），由 M_QUAL 推导
};

struct EncFeatureCollection {
    vector<EncFeature> features;
    map<string, vector<int>> index_by_code;  // 按要素代码索引
    ChartMetadata metadata;                  // 海图元信息汇总
};
```

### 5.5 坐标系与单位处理

S-57 中的坐标系统一为 WGS84（EPSG:4326），经纬度以度为单位。水深单位由 DSID 中的 DUNI 属性指定，可能是米、英寻（fathoms）或英尺（feet）。ENC Reader 在解析阶段必须将所有水深值统一转换为米。

| DUNI 值 | 单位 | 转换系数 |
|---------|------|---------|
| 1 | 米 | × 1.0 |
| 2 | 英寻及英尺 | × 1.8288（1 fathom = 6 feet） |
| 3 | 英尺 | × 0.3048 |
| 4 | 英寻及分数 | × 1.8288 |

高度单位（HUNI）同理：统一转换为米。

---

## 6. 第二层：空间索引层（Spatial Index）

### 6.1 索引结构

使用 R-tree（矩形树）对所有要素的 Bounding Box 建立空间索引。

**C++ 实现：** 使用 Boost.Geometry 的 R-tree 实现。

```cpp
#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

typedef bg::model::point<double, 2, bg::cs::geographic<bg::degree>> GeoPoint;
typedef bg::model::box<GeoPoint> GeoBox;
typedef std::pair<GeoBox, size_t> RTreeValue;  // bbox + feature index

bgi::rtree<RTreeValue, bgi::quadratic<16>> rtree_;
```

**Python 实现：** 使用 rtree 库。

```python
from rtree import index

idx = index.Index()
for i, feature in enumerate(features):
    idx.insert(i, feature.bbox)  # bbox = (min_lon, min_lat, max_lon, max_lat)
```

### 6.2 索引构建

在 ENC Reader 初始化时一次性构建：

```
FOR EACH feature IN feature_collection:
    计算 feature 的 bounding box
    插入 R-tree
```

### 6.3 分类索引

除了统一的 R-tree 之外，建议按要素类别建立独立的索引，以加速特定类型的查询：

| 索引名称 | 包含的要素代码 | 用途 |
|---------|-------------|------|
| depth_index | DEPARE, DEPCNT, SOUNDG, DRGARE | 水深查询 |
| obstruction_index | OBSTRN, WRECKS, UWTROC | 碍航物搜索 |
| restriction_index | RESARE, MIPARE, CTNARE | 限制区域检查 |
| tss_index | TSSLPT, TSSBND, TSSRON, TSEZNE, PRCARE | TSS 查询 |
| coastline_index | COALNE, LNDARE | 离岸距离计算 |
| clearance_index | BRIDGE, CBLOHD, PIPOHD | 净空校验 |
| aton_index | LIGHTS, BCNLAT, BOYLAT, BCNCAR, BOYCAR 等 | 助航标志查询 |

### 6.4 查询操作

R-tree 支持以下空间查询操作，是上层所有查询服务的基础：

**范围查询（Range Query）：** 给定一个矩形区域，返回所有 Bounding Box 与之相交的要素。时间复杂度 O(log n + k)，k 为结果数。

**K 近邻查询（KNN Query）：** 给定一个点，返回距离最近的 K 个要素。用于查找最近的碍航物、最近的岸线等。

**路径缓冲区查询（Path Buffer Query）：** 给定一条航线段，构造其缓冲区（矩形或胶囊形），查询缓冲区内的所有要素。这是最常用的查询模式。

缓冲区构造方法：

```
给定航线段 P1(lat1, lon1) → P2(lat2, lon2)，缓冲宽度 W：

1. 计算航线段的方位角 α = atan2(lon2-lon1, lat2-lat1)
2. 计算垂直方向 β = α ± 90°
3. 构造四个角点：
   C1 = P1 + W × direction(β)
   C2 = P1 - W × direction(β)
   C3 = P2 + W × direction(β)
   C4 = P2 - W × direction(β)
4. 缓冲区 = 矩形(C1, C2, C3, C4) 的 Bounding Box

注意：当航线段较长（> 50nm）时，需使用大圆距离计算，
直接用经纬度做矩形会有变形误差。
```

---

## 7. 第三层：语义解释层（Semantic Interpreter）

### 7.1 水深安全性判定

**输入：** 一个位置的水深值 d（来自 DEPARE/SOUNDG/DRGARE），本船吃水 T，当前水域类型的 UKC 要求 UKC_min，可选的潮汐预报和 Squat 修正。

**安全水深计算：**

```
d_required = T + UKC_min + S_squat + Δ_tide + Δ_zoc

其中：
  T         = 本船最大吃水（米）
  UKC_min   = 当前水域类型的最小 UKC 要求（米）
  S_squat   = Squat 下沉量（米），由速度和方形系数计算
  Δ_tide    = 潮汐不确定性余量（米），无潮汐预报时取 0.5m
  Δ_zoc     = ZOC 质量余量（米），见下表

判定结果：
  IF d ≥ d_required:       → SAFE（安全）
  IF d ≥ T + UKC_min:      → MARGINAL（边际安全，需注意）
  IF d < T + UKC_min:      → UNSAFE（不安全）
  IF d 未知（NaN）:         → UNKNOWN（未知，视为不安全）
```

**ZOC 质量余量表：**

| ZOC 等级 | 水深精度 | Δ_zoc |
|---------|---------|-------|
| A1 | ±(0.5m + 1% × d) | 0 m |
| A2 | ±(1.0m + 2% × d) | 0.5 m |
| B | ±(1.0m + 2% × d) | 1.0 m |
| C | ±(2.0m + 5% × d) | 2.0 m |
| D | 精度差 | 3.0 m |
| 未标注 | 假设最差 | 2.0 m |

### 7.2 碍航物危险性分级

**输入：** 一个碍航物要素（OBSTRN/WRECKS/UWTROC），本船吃水 T，UKC 要求。

**分级算法：**

```
FUNCTION classify_obstruction(feature, T, UKC_min):

    valsou = feature.attrs.get("VALSOU", NaN)
    watlev = feature.attrs.get("WATLEV", 0)
    
    # WATLEV 属性解释：
    # 1 = 干出（低潮时露出）
    # 2 = 始终干出（始终在水面以上）
    # 3 = 始终淹没
    # 4 = 涨潮时淹没（有时露出有时淹没）
    # 5 = 水面以上（浮体）
    # 6 = 水面齐平
    # 7 = 水面以下（始终在水下，但深度未知）

    IF watlev IN [1, 2, 5, 6]:
        # 可能露出水面或在水面上
        RETURN "CRITICAL", safe_distance = max(500m, 2 × 等深线范围)
    
    IF isnan(valsou):
        # 水深未知——最保守处理
        IF watlev == 7:
            RETURN "CRITICAL", safe_distance = max(500m, 2 × 等深线范围)
        ELSE:
            RETURN "UNKNOWN_CRITICAL", safe_distance = 500m
    
    d_required = T + UKC_min
    
    IF valsou < d_required:
        RETURN "CRITICAL", safe_distance = max(300m, 2 × 等深线范围)
    ELIF valsou < d_required + 2.0:
        RETURN "WARNING", safe_distance = max(200m, 1.5 × 等深线范围)
    ELSE:
        RETURN "SAFE", safe_distance = 100m
```

**特殊碍航物类型的额外处理：**

| CATOBS 值 | 类型 | 额外处理 |
|-----------|------|---------|
| 1 | 残骸（非沉船） | 可能有突出物，增大安全距离 50% |
| 2 | 沉船 | 可能有渔网/锚链缠绕，增大安全距离 |
| 5 | 渔具/渔栅 | 季节性存在，可能未在海图更新 |
| 6 | 钻井平台 | 周围 500m 安全区（OSPARE） |
| 7 | 海底设施 | 禁止抛锚 |
| 9 | 水下桩基 | 可能未完全标注 |

### 7.3 限制区域解析

**输入：** 一个 RESARE 要素。

**输出：**

```
struct RestrictionInfo {
    string area_name;          // 区域名称（如可用）
    string restriction_type;   // "no_entry" / "no_navigation" / "speed_limit" / "conditional" / "caution"
    bool must_avoid;           // true = 航线必须完全避开
    float speed_limit_knots;   // 限速值（如适用），NaN = 无限速
    string condition_text;     // 条件说明（如适用）
    TimeRange active_period;   // 生效时段（如适用，如军事演习区）
};

FUNCTION interpret_restriction(feature):
    restrn = feature.attrs.get("RESTRN", [])
    
    IF 3 IN restrn OR 7 IN restrn:
        RETURN {must_avoid: true, restriction_type: "no_entry"}
    
    IF 14 IN restrn:
        speed_val = feature.attrs.get("VSPD", NaN)  # 速度限制值（如有）
        RETURN {must_avoid: false, restriction_type: "speed_limit", speed_limit_knots: speed_val}
    
    IF 9 IN restrn:
        RETURN {must_avoid: true, restriction_type: "construction_zone"}
    
    IF 13 IN restrn:
        RETURN {must_avoid: false, restriction_type: "diving_area", speed_limit_knots: 5.0}
    
    # 其他限制类型不影响航行但应记录
    RETURN {must_avoid: false, restriction_type: "info_only"}
```

### 7.4 TSS 方向与合规性校验

**输入：** 航线段方位角 φ_route，TSSLPT 要素的 ORIENT 属性值 φ_tss。

**校验逻辑：**

```
FUNCTION check_tss_compliance(φ_route, φ_tss):
    Δφ = normalize_angle(φ_route - φ_tss)   # 归一化到 [-180, 180]
    
    IF |Δφ| ≤ 15:
        RETURN "COMPLIANT"          # 航线方向与 TSS 方向一致
    ELIF |Δφ| > 165:
        RETURN "AGAINST_TRAFFIC"    # 逆向通过，严重违规
    ELIF |Δφ| ≤ 90:
        RETURN "CROSSING_ACCEPTABLE"  # 可能是横穿 TSS，需检查是否在穿越区
    ELSE:
        RETURN "CROSSING_LARGE_ANGLE" # 大角度穿越，应尽量避免
```

### 7.5 垂直净空校验

**输入：** 桥梁/架空电缆要素的 VERCLR 属性，本船最高点离水高度（Air Draft）。

```
FUNCTION check_vertical_clearance(feature, air_draft):
    verclr = feature.attrs.get("VERCLR", NaN)
    
    IF isnan(verclr):
        RETURN {passable: false, reason: "净空高度未知，不可通过"}
    
    margin = verclr - air_draft
    
    IF margin < 0:
        RETURN {passable: false, reason: f"净空不足：需要{air_draft}m，仅有{verclr}m"}
    ELIF margin < 1.0:
        RETURN {passable: true, warning: f"净空余量仅{margin:.1f}m，需考虑潮位和波浪影响"}
    ELSE:
        RETURN {passable: true, margin: margin}
```

### 7.6 水域类型自动分类

**输入：** 一个坐标点。

**分类算法：**

```
FUNCTION classify_zone(point, ship_params):

    # 1. 检查是否在特定管理区域内
    IF point_in_any(point, TSSLPT_features):
        tss_orient = get_tss_orientation(point)
        RETURN {zone_type: "tss_lane", tss_direction: tss_orient}
    
    IF point_in_any(point, ACHARE_features):
        RETURN {zone_type: "anchorage"}
    
    # 2. 检查港口距离
    dist_to_port = min_distance(point, port_entrance_features)
    IF dist_to_port < 1.0 nm:
        RETURN {zone_type: "port_inner"}
    IF dist_to_port < 5.0 nm:
        RETURN {zone_type: "port_approach"}
    
    # 3. 获取水深和离岸距离
    depth = get_depth_at(point)
    dist_to_shore = min_distance(point, COALNE_features)
    h_over_T = depth / ship_params.draft
    
    # 4. 检查航道宽度（如在航道内）
    IF point_in_any(point, FAIRWY_features):
        channel_width = get_channel_width(point)
        IF channel_width < 10 * ship_params.beam:
            RETURN {zone_type: "narrow_channel", channel_width: channel_width}
    
    # 5. 按水深和离岸距离分类
    IF h_over_T < 3.0:
        RETURN {zone_type: "shallow", h_over_T: h_over_T}
    IF dist_to_shore < 10.0 nm:
        RETURN {zone_type: "coastal", distance_to_shore: dist_to_shore}
    
    RETURN {zone_type: "open_sea"}
```

---

## 8. 第四层：查询服务层（Query Service）

### 8.1 查询一：航线段安全检查（Route Leg Check）

**功能：** 对一条航线段进行全面安全评估。这是 WP Generator 最频繁调用的接口。

**输入参数：**

```
struct RouteLegCheckInput {
    GeoPoint start;              // 航线段起点（纬度、经度）
    GeoPoint end;                // 航线段终点
    float buffer_width_m;        // 检查缓冲区宽度（安全走廊半宽），米
    ShipParams ship;             // 本船参数（吃水、宽度、高度）
    float planned_speed_knots;   // 计划航速（用于 Squat 计算）
};
```

**处理过程：**

```
FUNCTION check_route_leg(input):

    # 1. 构造缓冲区
    buffer = create_buffer(input.start, input.end, input.buffer_width_m)
    
    # 2. 陆地穿越检查
    land_features = query_rtree(buffer, LNDARE_index)
    FOR EACH land IN land_features:
        IF line_intersects_polygon(input.start, input.end, land.geometry):
            result.crosses_land = true
            result.is_safe = false
    
    # 3. 水深检查
    depth_profile = sample_depth_along_leg(input.start, input.end, step=10m)
    result.min_depth = min(depth_profile.depths)
    d_required = compute_required_depth(input.ship, input.planned_speed_knots)
    IF result.min_depth < d_required:
        result.depth_safe = false
        result.is_safe = false
    
    # 4. 碍航物检查
    obstructions = query_rtree(buffer, obstruction_index)
    FOR EACH obs IN obstructions:
        danger = classify_obstruction(obs, input.ship.draft, UKC_min)
        IF danger.level IN ["CRITICAL", "UNKNOWN_CRITICAL"]:
            IF distance_to_leg(obs, input.start, input.end) < danger.safe_distance:
                result.dangerous_obstructions.append(obs, danger)
                result.is_safe = false
    
    # 5. 限制区域检查
    restrictions = query_rtree(buffer, restriction_index)
    FOR EACH res IN restrictions:
        IF line_intersects_polygon(input.start, input.end, res.geometry):
            info = interpret_restriction(res)
            IF info.must_avoid:
                result.intersected_restrictions.append(res, info)
                result.is_safe = false
            ELSE:
                result.warnings.append(res, info)
    
    # 6. TSS 合规性检查
    tss_areas = query_rtree(buffer, tss_index)
    FOR EACH tss IN tss_areas:
        IF line_intersects_polygon(input.start, input.end, tss.geometry):
            route_bearing = compute_bearing(input.start, input.end)
            compliance = check_tss_compliance(route_bearing, tss.orient)
            IF compliance == "AGAINST_TRAFFIC":
                result.crosses_tss_against_traffic = true
                result.is_safe = false
    
    # 7. 垂直净空检查
    clearance_features = query_rtree(buffer, clearance_index)
    FOR EACH clr IN clearance_features:
        IF line_intersects_line(input.start, input.end, clr.geometry):
            check = check_vertical_clearance(clr, input.ship.air_draft)
            result.clearance_checks.append(clr, check)
            IF NOT check.passable:
                result.is_safe = false
    
    RETURN result
```

**输出结构：**

```
struct RouteLegCheckResult {
    bool is_safe;                              // 总体判定
    bool crosses_land;                         // 是否穿越陆地
    bool crosses_tss_against_traffic;          // 是否逆向穿越 TSS
    bool depth_safe;                           // 水深是否满足要求
    float min_depth;                           // 沿线最浅水深（米）
    float min_ukc;                             // 最小 UKC（米）
    float required_depth;                      // 要求的最小水深（米）
    ObstructionDanger[] dangerous_obstructions; // 危险碍航物列表
    RestrictionViolation[] intersected_restrictions; // 穿越的限制区域
    ClearanceCheck[] clearance_checks;         // 净空校验结果
    Warning[] warnings;                        // 非致命警告列表
    ZocLevel worst_zoc;                        // 沿线最差 ZOC 等级
};
```

### 8.2 查询二：可用走廊宽度（Available Corridor Width）

**功能：** 计算航点两侧可用的安全水域宽度。WP Generator 的转弯评估依赖此接口。

**输入参数：**

```
struct CorridorWidthInput {
    GeoPoint waypoint;         // 航点位置
    float bearing_in;          // 进入方向（度）
    float bearing_out;         // 离开方向（度）
    ShipParams ship;           // 本船参数
    float max_search_width_m;  // 最大搜索宽度（默认 2000m）
    float step_m;              // 搜索步长（默认 10m）
};
```

**处理过程：**

```
FUNCTION get_corridor_width(input):
    
    # 计算垂直于航线的搜索方向
    avg_bearing = (input.bearing_in + input.bearing_out) / 2
    search_dir_port = avg_bearing - 90      # 左舷方向
    search_dir_stbd = avg_bearing + 90      # 右舷方向
    
    # 向左舷搜索
    width_port = 0
    FOR d = input.step_m TO input.max_search_width_m STEP input.step_m:
        test_point = offset_point(input.waypoint, search_dir_port, d)
        
        # 检查该点是否安全
        depth = get_depth_at(test_point)
        d_required = compute_required_depth(input.ship, ...)
        
        IF depth < d_required:
            width_port = d - input.step_m
            limiting_factor_port = "depth"
            BREAK
        
        IF point_in_any(test_point, LNDARE_features):
            width_port = d - input.step_m
            limiting_factor_port = "land"
            BREAK
        
        IF point_in_any(test_point, restriction_no_entry_features):
            width_port = d - input.step_m
            limiting_factor_port = "restriction"
            BREAK
        
        nearest_obs = find_nearest_obstruction(test_point, critical_only=true)
        IF nearest_obs AND nearest_obs.distance < nearest_obs.safe_distance:
            width_port = d - input.step_m
            limiting_factor_port = "obstruction"
            BREAK
        
        width_port = d  // 到达最大搜索宽度仍安全
    
    # 向右舷方向同理搜索（代码对称，省略）
    
    RETURN {width_port, width_starboard, limiting_factor_port, limiting_factor_stbd}
```

### 8.3 查询三：沿航线水深剖面（Depth Profile）

**功能：** 获取航线段沿线的水深分布，用于安全检查和速度规划。

**输入参数：**

```
struct DepthProfileInput {
    GeoPoint start;
    GeoPoint end;
    float sample_interval_m;   // 采样间距（默认 50m）
    bool include_tide;         // 是否包含潮汐修正
};
```

**处理过程：**

```
FUNCTION get_depth_profile(input):
    
    total_distance = geo_distance(input.start, input.end)
    n_samples = ceil(total_distance / input.sample_interval_m)
    
    samples = []
    FOR i = 0 TO n_samples:
        fraction = i / n_samples
        sample_point = interpolate(input.start, input.end, fraction)
        
        # 获取水深：优先使用 SOUNDG，其次 DEPARE，再次 DRGARE
        depth = NaN
        source = "none"
        
        # 查找最近的测深点
        nearest_sounding = find_nearest(sample_point, SOUNDG_index, max_dist=100m)
        IF nearest_sounding:
            depth = nearest_sounding.depth
            source = "sounding"
        
        # 如果没有测深点，使用水深区域
        IF isnan(depth):
            depth_area = find_containing_area(sample_point, DEPARE_index)
            IF depth_area:
                depth = depth_area.attrs["DRVAL1"]  # 取浅限值（保守）
                source = "depth_area"
        
        # 如果在疏浚区域内，使用疏浚深度（扣除淤积余量）
        dredged = find_containing_area(sample_point, DRGARE_index)
        IF dredged:
            d_dredged = dredged.attrs["DRVAL1"] - SILTATION_ALLOWANCE
            depth = min(depth, d_dredged) IF NOT isnan(depth) ELSE d_dredged
            source = "dredged_area"
        
        # 潮汐修正（如可用）
        depth_with_tide = depth
        IF input.include_tide AND tide_model_available:
            tide_height = get_tide_prediction(sample_point, current_time)
            depth_with_tide = depth + tide_height
        
        # ZOC 等级
        zoc = get_zoc_at(sample_point)
        
        samples.append({
            distance_along: fraction * total_distance,
            lat: sample_point.lat,
            lon: sample_point.lon,
            depth: depth,
            depth_with_tide: depth_with_tide,
            source: source,
            zoc: zoc
        })
    
    RETURN {samples: samples, min_depth: min(s.depth for s in samples)}
```

**淤积余量常数：**

```
SILTATION_ALLOWANCE = 0.3 ~ 0.5 m（可配置）
```

### 8.4 查询四：水域类型分类（Zone Classification）

详见第 7.6 节的分类算法。此处定义其接口格式。

**输出结构：**

```
struct ZoneClassification {
    string zone_type;              // 水域类型标识
    float h_over_T;                // 水深/吃水比
    float distance_to_shore_m;     // 离最近岸线距离
    float channel_width_m;         // 航道宽度（如在航道内）
    string[] active_restrictions;  // 生效的限制列表
    float speed_limit_knots;       // 限速值（NaN = 无限速）
    float tss_direction_deg;       // TSS 通航方向（NaN = 不在 TSS 内）
    string tss_name;               // TSS 名称（如可用）
};
```

### 8.5 查询五：碍航物搜索（Obstruction Search）

**输入参数：**

```
struct ObstructionSearchInput {
    GeoPoint center;               // 搜索中心
    float radius_m;                // 搜索半径
    bool critical_only;            // 仅返回危险级碍航物
};
```

**输出：** 按距离排序的碍航物列表，每个条目包含位置、类型、水深、危险等级、建议安全距离。

### 8.6 查询六：助航标志查询（AtoN Query）

**输入参数：**

```
struct AtoNQueryInput {
    GeoPoint start;                // 航线段起点
    GeoPoint end;                  // 航线段终点
    float corridor_width_nm;       // 查询走廊宽度（海里）
};
```

**输出：** 沿航线可见的助航标志列表，每个条目包含类型、位置、灯质、射程、颜色、IALA 类别。

---

## 9. 数据完整性与质量检查

### 9.1 海图时效性检查

**检查项目：**

```
FUNCTION check_chart_currency(chart_metadata):
    
    days_since_update = (current_date - chart_metadata.update_date).days
    
    IF days_since_update > 90:
        RETURN {level: "CRITICAL", message: f"海图 {chart_metadata.id} 超过 90 天未更新"}
    IF days_since_update > 30:
        RETURN {level: "WARNING", message: f"海图 {chart_metadata.id} 超过 30 天未更新"}
    IF days_since_update > 14:
        RETURN {level: "INFO", message: f"海图 {chart_metadata.id} 超过 14 天未更新，建议检查是否有新版本"}
    
    RETURN {level: "OK"}
```

### 9.2 ZOC 覆盖检查

**检查航线沿途的 ZOC 等级分布：**

```
FUNCTION check_zoc_coverage(route_legs):
    
    FOR EACH leg IN route_legs:
        zoc_profile = sample_zoc_along_leg(leg, step=500m)
        
        worst_zoc = max(zoc_profile)  # D 最差，A1 最好
        
        IF worst_zoc >= "C":
            warnings.append({
                leg: leg,
                zoc: worst_zoc,
                message: f"航段 {leg.index} 经过 ZOC {worst_zoc} 区域，测深数据可靠性较低"
            })
        
        IF worst_zoc >= "D":
            critical.append({
                leg: leg,
                message: f"航段 {leg.index} 经过 ZOC D 区域，建议更换航路或增大安全余量"
            })
    
    RETURN {warnings, critical}
```

### 9.3 大比例尺覆盖缺失检测

**检查航线是否经过缺少详图覆盖的区域：**

```
FUNCTION check_scale_coverage(route_legs, min_required_scale):
    
    # min_required_scale 建议值：
    # 港内：1:10,000
    # 沿岸：1:50,000
    # 近海：1:150,000
    # 远洋：1:500,000
    
    FOR EACH leg IN route_legs:
        zone = classify_zone(leg.midpoint)
        required = get_required_scale(zone.zone_type)
        
        best_available = get_largest_scale_at(leg.midpoint)
        
        IF best_available > required:  # 比例尺分母越大越粗略
            warnings.append({
                leg: leg,
                available_scale: best_available,
                required_scale: required,
                message: f"航段 {leg.index} 缺少 1:{required} 或更大比例尺海图覆盖"
            })
```

**各水域类型推荐的最小海图比例尺：**

| 水域类型 | 推荐最小比例尺 | 说明 |
|---------|-------------|------|
| port_inner | 1:10,000 | 港内需要最详细的海图 |
| port_approach | 1:25,000 | 进港航道 |
| narrow_channel | 1:25,000 | 狭水道 |
| coastal | 1:50,000~1:150,000 | 沿岸航行 |
| open_sea | 1:500,000 或更小 | 远洋不需要大比例尺 |

---

## 10. 安全等深线自动选取

### 10.1 概念

安全等深线（Safety Contour）是船长在海图上设定的一条关键参考线——水深等于或大于本船安全通过所需最小水深的等深线。在 ECDIS 中，安全等深线以粗蓝线显示，安全等深线以浅的区域以蓝色填充（表示危险），以深的区域保持白色（表示安全）。

### 10.2 计算公式

```
Safety_Contour_Value = T + UKC_min + S_squat_max + Δ_tide + Δ_zoc

其中：
  T              = 本船最大静态吃水（米）
  UKC_min        = 当前水域类型的最小 UKC（米）
  S_squat_max    = 最大预期 Squat 量（基于最高计划速度和方形系数）
  Δ_tide         = 潮汐不确定性余量：
                   有可靠潮汐预报时 = 0.3m
                   无潮汐预报时 = 0.5m（假设最低潮位）
  Δ_zoc          = ZOC 质量余量（按沿线最差 ZOC 等级取值）
```

**算例：**

```
T = 0.8m, UKC_min = 1.0m, S_squat_max = 0.2m, Δ_tide = 0.5m, Δ_zoc = 1.0m（ZOC B）
Safety_Contour_Value = 0.8 + 1.0 + 0.2 + 0.5 + 1.0 = 3.5m
→ 选取 4m 等深线作为安全等深线（取海图上存在的 ≥ 3.5m 的最近等深线）
```

### 10.3 标准等深线值

S-57 中常用的等深线值序列：0, 2, 5, 10, 15, 20, 30, 50, 100, 200, 500, 1000 米。但各国水文局可能使用不同的等深线间隔。ENC Reader 应扫描实际可用的 DEPCNT 值集合，从中选取最接近且不小于 Safety_Contour_Value 的等深线。

### 10.4 安全等深线的用途

安全等深线在 ENC Reader 中有两个关键用途：

1. **走廊宽度计算的边界：** 在查询二（Available Corridor Width）中，安全等深线是搜索终止条件之一——搜索到安全等深线即停止，该方向上的可用宽度到此为止。

2. **航线段安全检查的快速筛选：** 在查询一（Route Leg Check）中，如果航线段完全位于安全等深线以深的区域，可以跳过逐点水深检查，大幅提升性能。

---

## 11. 特殊要素处理

### 11.1 干出区域与潮间带

DEPARE 或 SOUNDG 的水深值为负数时，表示干出高度（低潮时露出水面的高度）。

**处理规则：**

```
IF depth < 0:
    # 干出区域
    IF tide_model_available:
        actual_depth = depth + tide_height    # depth 为负，tide_height 为正
        IF actual_depth > d_required:
            → 当前潮位可安全通过（但风险高，不建议在航线规划中使用）
        ELSE:
            → 不可通过
    ELSE:
        → 视为陆地，完全禁止通过
```

### 11.2 疏浚区域

DRGARE 标注的是人工疏浚维持的航道深度。

**特殊处理：**

```
FUNCTION interpret_dredged_area(feature):
    
    maintained_depth = feature.attrs["DRVAL1"]
    survey_date = feature.attrs.get("SORDAT", "unknown")
    
    # 计算自上次测量以来的时间
    IF survey_date != "unknown":
        days_since_survey = (current_date - parse_date(survey_date)).days
        
        # 淤积扣减（经验值，可按区域配置）
        siltation_rate = 0.01   # 米/天，保守估计
        siltation_amount = min(siltation_rate * days_since_survey, 1.0)  # 上限 1m
    ELSE:
        siltation_amount = SILTATION_ALLOWANCE_DEFAULT  # 默认 0.5m
    
    effective_depth = maintained_depth - siltation_amount
    
    RETURN {
        maintained_depth: maintained_depth,
        effective_depth: effective_depth,
        siltation_deduction: siltation_amount,
        survey_date: survey_date,
        warning: "疏浚区域，实际水深可能因淤积而浅于标注值"
    }
```

### 11.3 虚拟航标

S-57 中，某些助航标志标注为虚拟航标（通过 AIS 广播的虚拟标志位）。其 STATUS 属性包含值 18（Virtual AIS AtoN）。

```
FUNCTION is_virtual_aton(feature):
    status = feature.attrs.get("STATUS", [])
    RETURN 18 IN status
```

虚拟航标在物理世界中不存在实体，ENC Reader 应在助航标志查询结果中标注此信息，提醒感知系统不要期望在雷达或摄像头上探测到这些标志。

### 11.4 可移动桥梁

BRIDGE 要素可能有两个净空高度：VERCCL（桥梁关闭时的净空）和 VERCOP（桥梁开启时的净空）。

```
IF feature.has("VERCOP"):
    # 可移动桥梁（开合桥/升降桥/旋转桥）
    → 关闭时净空 = VERCCL
    → 开启时净空 = VERCOP
    → 如果 VERCCL < ship.air_draft:
        → 需要申请开桥，记录在航线元数据中
        → 可能有开桥时间窗口限制
ELSE:
    # 固定桥梁
    → 净空 = VERCLR
    → 如果不满足则不可通过，需绕行
```

### 11.5 海底电缆和管线

CBLSUB（海底电缆）和 PIPSOL（海底管线）通常不影响水面航行，但影响锚泊和疏浚作业。

```
处理规则：
- 航行可以通过，不视为碍航物
- 但如果航线规划包含锚泊段，锚泊位置不得位于海底电缆/管线上方
- 在海底管线区域，如果管线可能浅埋段突出海底（BURDEP 属性 = 0 或未标注），
  应降低该区域的有效水深
```

### 11.6 海洋养殖区

MARCUL（Marine Culture/Farm）标注的是海洋养殖区域（渔排、网箱、养殖绳等）。

```
处理规则：
- 不是正式的禁航区，但实际上无法通过
- 视为碍航物处理，安全距离 ≥ 200m
- 养殖设施可能有季节性变化，海图更新可能滞后
```

---

## 12. 海图拼接与多比例尺管理

### 12.1 多海图加载

一个跨洋航次可能涉及数十幅不同比例尺的 ENC。ENC Reader 的加载策略：

```
FUNCTION load_charts_for_route(route, chart_catalog):
    
    # 1. 根据航线的 Bounding Box 确定涉及的海图列表
    route_bbox = compute_route_bbox(route, margin=10nm)
    
    candidate_charts = []
    FOR EACH chart IN chart_catalog:
        IF chart.bbox intersects route_bbox:
            candidate_charts.append(chart)
    
    # 2. 按比例尺排序：大比例尺优先
    candidate_charts.sort(by=lambda c: c.scale)  # scale 小 = 大比例尺 = 更详细
    
    # 3. 逐张加载并解析
    FOR EACH chart IN candidate_charts:
        features = parse_s57(chart.filepath)
        add_to_collection(features, chart_metadata=chart)
    
    # 4. 构建统一空间索引
    build_rtree(all_features)
```

### 12.2 重叠区域处理

当多幅不同比例尺的海图在同一区域重叠时，遵循以下优先级规则：

```
原则：大比例尺（小 scale 分母）数据优先于小比例尺数据。

实现方式：
1. 每个要素记录其来源海图的比例尺（chart_scale 字段）
2. 查询返回的候选要素集中，如果同一类型有多个要素覆盖查询点，
   取比例尺最大（scale 分母最小）的那个
3. 特例：SOUNDG（测深点）不去重——所有比例尺的测深点都保留，
   取最浅值（最保守）
```

### 12.3 海图覆盖缺口检测

```
FUNCTION detect_coverage_gaps(route):
    
    FOR EACH sample_point ALONG route AT step=1nm:
        charts_covering = find_charts_containing(sample_point)
        
        IF len(charts_covering) == 0:
            gaps.append({
                position: sample_point,
                message: "该位置无任何 ENC 覆盖"
            })
        
        best_scale = min(c.scale for c in charts_covering)
        required_scale = get_required_scale_for_zone(sample_point)
        
        IF best_scale > required_scale:
            warnings.append({
                position: sample_point,
                available_scale: best_scale,
                required_scale: required_scale
            })
    
    RETURN {gaps, warnings}
```

---

## 13. 性能要求与优化策略

### 13.1 性能指标

| 操作 | 目标延迟 | 说明 |
|------|---------|------|
| 初始化（加载全部海图 + 建索引） | < 60 秒 | 航次规划阶段一次性执行 |
| 单次 Route Leg Check | < 10 ms | WP Generator 主循环调用 |
| 单次 Corridor Width 查询 | < 20 ms | 涉及射线步进，可能较慢 |
| 单次 Depth Profile（1km 航段） | < 5 ms | |
| 单次 Zone Classification | < 1 ms | |
| 单次 Obstruction Search（500m 半径） | < 5 ms | |

### 13.2 优化策略

**预计算安全等深线多边形：** 初始化时将安全等深线提取为闭合多边形，后续的"是否在安全水深区域内"判定变成简单的点-多边形包含测试，比逐点查水深快两个数量级。

**分区域懒加载：** 对于超长航次，不在启动时加载全部海图，而是根据当前船位和前方 50nm 的航线段动态加载所需海图。卸载已经通过且不再需要的海图以释放内存。

**查询结果缓存：** 对于同一航线段的重复查询（WP Generator 可能在调整航点位置时多次查询相近区域），缓存上次查询结果，仅当查询区域变化超过阈值时才重新执行。

**并行化：** 对多段航线的安全检查可以并行执行（每段独立，无依赖关系）。C++ 可用 OpenMP 或 std::async，Python 可用 concurrent.futures。

---

## 14. ROS2 接口设计

### 14.1 节点定义

ENC Reader 作为 Voyage Planner 的内部子模块，嵌入在 `voyage_planner_node` 进程中，通过函数调用被 WP Generator 直接使用。同时通过 ROS2 Service 对外暴露查询接口，供调试工具和其他模块使用。

### 14.2 ROS2 Service 接口

| 服务名称 | 请求类型 | 响应类型 | 说明 |
|---------|---------|---------|------|
| `/voyage/enc/check_route_leg` | RouteLegCheckRequest | RouteLegCheckResponse | 航线段安全检查 |
| `/voyage/enc/get_corridor_width` | CorridorWidthRequest | CorridorWidthResponse | 可用走廊宽度 |
| `/voyage/enc/get_depth_profile` | DepthProfileRequest | DepthProfileResponse | 沿航线水深剖面 |
| `/voyage/enc/classify_zone` | ZoneClassifyRequest | ZoneClassifyResponse | 水域类型分类 |
| `/voyage/enc/search_obstructions` | ObstructionSearchRequest | ObstructionSearchResponse | 碍航物搜索 |
| `/voyage/enc/get_chart_info` | ChartInfoRequest | ChartInfoResponse | 海图元信息 |

### 14.3 初始化发布

ENC Reader 在初始化完成后，发布一次性的海图元信息摘要至话题 `/voyage/enc/chart_summary`（QoS = TRANSIENT_LOCAL），内容包括：已加载海图数量、总覆盖范围、最旧更新日期、ZOC 分布统计。

---

## 15. 与其他模块的协作关系

| 调用方 | 使用的查询接口 | 调用频率 | 说明 |
|-------|-------------|---------|------|
| WP Generator — Turn Evaluator | Corridor Width | 每航点一次 | 获取转弯可用空间 |
| WP Generator — ENC Query 步骤 | Route Leg Check | 每航段一次 | 验证航段安全性 |
| WP Generator — Special Zone Handler | Zone Classification | 每航点一次 | 识别特殊水域 |
| Speed Profiler | Zone Classification | 每航段一次 | 获取限速约束 |
| Safety Checker | Route Leg Check | 全航线一次 | 最终安全复核 |
| Tactical Layer（L3） | Route Leg Check | 按需 | 验证避碰航线安全性 |
| Guidance Layer（L4） | Depth Profile | 按需 | 运行时水深监控 |

---

## 16. 实现建议与开发路线

### 16.1 技术栈推荐

| 组件 | 推荐技术 | 说明 |
|------|---------|------|
| S-57 解析 | GDAL/OGR (C++ 或 Python) | 工业级标准库 |
| 空间索引 | Boost.Geometry R-tree (C++) 或 rtree (Python) | 毫秒级查询 |
| 几何计算 | Boost.Geometry (C++) 或 Shapely (Python) | 相交检测、距离计算 |
| 大圆距离 | GeographicLib (C++) 或 geopy (Python) | 高精度大地测量 |
| 坐标转换 | PROJ (C++) 或 pyproj (Python) | WGS84 与本地坐标互转 |

### 16.2 开发阶段

**Phase 1（2~3 周）：** 数据解析 + 空间索引。目标：能加载 S-57 文件，建立 R-tree，执行基本的范围查询。验证：在测试海图上查询指定区域的所有要素，确认结果正确。

**Phase 2（3~4 周）：** 语义解释层核心。目标：实现水深安全判定、碍航物分级、限制区域解析。验证：对已知航线执行安全检查，与人工审图结果对比。

**Phase 3（2~3 周）：** 完整查询服务层。目标：实现全部六类查询接口，集成到 WP Generator。验证：端到端生成航线，航线不穿越危险区域。

**Phase 4（2 周）：** 数据质量检查 + 多比例尺管理。目标：ZOC 检查、时效性检查、覆盖缺口检测、多海图拼接。验证：跨多幅海图的长距离航线正确拼接。

---

## 17. 测试策略与验收标准

### 17.1 测试场景

| 场景编号 | 场景描述 | 验证目标 |
|---------|---------|---------|
| ENC-001 | 加载单幅 S-57 海图 | 解析正确性，要素数量校验 |
| ENC-002 | 航线段穿越陆地 | crosses_land 检测 |
| ENC-003 | 航线段经过浅水区 | 最小水深检测，UKC 校验 |
| ENC-004 | 航线段经过沉船 | 碍航物检测与分级 |
| ENC-005 | 航线段穿越禁航区 | 限制区域检测 |
| ENC-006 | 航线段逆向穿越 TSS | TSS 合规性检测 |
| ENC-007 | 航线段经过桥梁 | 垂直净空校验 |
| ENC-008 | 走廊宽度查询（开阔水域） | 返回正确的两侧宽度 |
| ENC-009 | 走廊宽度查询（狭窄航道） | 正确检测航道边界 |
| ENC-010 | 水域类型分类——港内 | 返回 "port_inner" |
| ENC-011 | 水域类型分类——TSS 内 | 返回 "tss_lane" 及方向 |
| ENC-012 | 水域类型分类——浅水 | 返回 "shallow" 及 h/T 值 |
| ENC-013 | 多比例尺海图拼接 | 大比例尺数据优先 |
| ENC-014 | ZOC 质量检查 | 低 ZOC 区域告警 |
| ENC-015 | 海图时效性检查 | 过期海图告警 |
| ENC-016 | 干出区域处理 | 负水深正确处理 |
| ENC-017 | 疏浚区域处理 | 淤积扣减正确应用 |
| ENC-018 | 查询性能测试 | 单次查询 < 10ms |

### 17.2 验收标准

| 指标 | 标准 |
|------|------|
| 陆地穿越检测 | 零漏检 |
| 碍航物检测（CRITICAL 级） | 零漏检 |
| 禁航区检测 | 零漏检 |
| TSS 逆向检测 | 零漏检 |
| 水深安全判定误差 | ≤ 0.1m（与人工审图结果对比） |
| 走廊宽度误差 | ≤ 查询步长（默认 10m） |
| 单次查询延迟 | ≤ 10ms（Route Leg Check） |
| 初始化时间 | ≤ 60s（加载 50 幅海图） |
| 内存占用 | ≤ 500 MB（50 幅海图） |

---

## 18. 参考文献与标准

| 编号 | 文献/标准 | 相关内容 |
|------|---------|---------|
| [1] | IHO S-57 Ed. 3.1 | 电子海图数据传输标准 |
| [2] | IHO S-52 Ed. 6 | ECDIS 显示标准（含安全等深线定义） |
| [3] | IHO S-100 | 通用水文数据模型（下一代标准） |
| [4] | IHO S-101 | S-100 框架下的 ENC 产品规范 |
| [5] | IHO S-44 Ed. 6 | 水深测量标准（ZOC 定义） |
| [6] | IMO A.817(19) / MSC.232(82) | ECDIS 性能标准 |
| [7] | IMO COLREGs 第十条 | 分道通航制规定 |
| [8] | GDAL/OGR S-57 驱动文档 | https://gdal.org/drivers/vector/s57.html |
| [9] | PIANC "Harbour Approach Channels Design Guidelines" 2014 | UKC 标准、航道设计 |
| [10] | Barrass, C.B. "Ship Design and Performance" 2004 | Squat 效应计算 |

---

## v3.1 补充升级：港口规则数据库

### A. 问题——港内规则不在 S-57 海图中

S-57 电子海图包含水深、航标、TSS 等通用航海信息。但各港口特有的规则——限速区、单向航道、强制引航区、锚泊区限制、VTS 管制频率——不在标准海图数据中。这些规则来自地方港务局的法规和港口手册。

### B. 港口规则数据补充图层

```
# 港口规则以 GeoJSON 补充图层的形式加载，叠加在 S-57 海图之上

PortRulesLayer 数据格式（每个港口一个 JSON 文件）：

{
    "port_id": "CNSHA",
    "port_name": "Shanghai Waigaoqiao",
    "rules": [
        {
            "type": "speed_limit_zone",
            "polygon": [[121.50, 31.35], [121.52, 31.35], ...],
            "max_speed_kn": 8,
            "applies_to": ["all"],
            "source": "Shanghai MSA Notice 2025-047"
        },
        {
            "type": "one_way_channel",
            "polyline": [[121.51, 31.34], [121.51, 31.36], ...],
            "direction": 0,
            "width_m": 200,
            "source": "Shanghai Port Regulation Art. 23"
        },
        {
            "type": "mandatory_pilotage_zone",
            "polygon": [...],
            "vhf_channel": 16,
            "pilot_boarding_point": [121.505, 31.345]
        },
        {
            "type": "anchorage_area",
            "polygon": [...],
            "max_draft_m": 10,
            "holding_ground": "mud"
        },
        {
            "type": "berth_definition",
            "id": "WGQ-7",
            "position": [121.508, 31.352],
            "heading_deg": 270,
            "length_m": 60,
            "depth_m": 8.5,
            "approach_heading_deg": 180,
            "approach_length_m": 150,
            "fenders": "pneumatic",
            "mooring_points": 6,
            "reflector_installed": true
        }
    ]
}
```

### C. 港口规则查询接口

```
FUNCTION query_port_rules(position, heading, speed):
    
    rules_violated = []
    
    # 限速区检查
    FOR EACH zone IN port_rules.speed_limit_zones:
        IF point_in_polygon(position, zone.polygon):
            IF speed > zone.max_speed_kn:
                rules_violated.append({
                    type: "SPEED_LIMIT",
                    allowed: zone.max_speed_kn,
                    actual: speed,
                    source: zone.source
                })
    
    # 单向航道检查
    FOR EACH channel IN port_rules.one_way_channels:
        IF point_near_polyline(position, channel.polyline, channel.width_m / 2):
            heading_diff = abs(normalize_pm180(heading - channel.direction))
            IF heading_diff > 30:    # 航向偏差 > 30° → 逆行
                rules_violated.append({
                    type: "WRONG_WAY",
                    required_heading: channel.direction,
                    actual_heading: heading
                })
    
    # 强制引航区检查
    FOR EACH zone IN port_rules.mandatory_pilotage_zones:
        IF point_in_polygon(position, zone.polygon):
            rules_violated.append({
                type: "PILOTAGE_REQUIRED",
                vhf: zone.vhf_channel
            })
    
    RETURN rules_violated

# 发布话题：/navigation/port_rules_status
# 频率：1 Hz
# 供 L3 Risk Monitor + HMI 显示 + ASDR 记录
```

### D. 泊位数据库

港口规则文件中的 `berth_definition` 条目提供了靠泊路径规划所需的全部信息——泊位位置、朝向、进近方向、深度、反射器安装状态。L2 Berth Planner 直接读取此数据。

---

## 变更记录

| 版本 | 日期 | 作者 | 变更内容 |
|------|------|------|---------|
| 0.1 | 2026-04-25 | 架构组 | 初始版本 |
| 0.2 | 2026-04-26 | 架构组 | v3.1 补充：增加港口规则数据库（GeoJSON 补充图层——限速区/单向航道/强制引航区/锚泊区/泊位定义）；增加港口规则实时查询接口 |

---

**文档结束**
