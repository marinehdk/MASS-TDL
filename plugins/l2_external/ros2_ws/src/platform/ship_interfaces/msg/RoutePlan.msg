# ============================================================
# RoutePlan.msg — 航线规划模块 → MASS ADAS
# 坐标系: WGS84 经纬度，第一个航点为本地原点
# ============================================================
std_msgs/Header header

# ---- 必填字段 ----
float64[] latitude            # 纬度数组（度，WGS84），至少1个
float64[] longitude           # 经度数组（度，WGS84），与latitude等长

# ---- 可选字段（数组长度必须与latitude相同，0/空表示不限制）----
float64[] speed_limit_mps     # 每个航点的速度限制（m/s），0=不限速
string[] navigation_mode      # 每个航点的导航模式提示
                               #   "cruise"         - 开阔水域巡航
                               #   "narrow_channel" - 窄航道
                               #   "harbor"         - 港内机动
                               #   "approach"       - 进近
                               #   "dp_hold"        - 到达后DP保持（不继续过点）
                               #   ""               - 不指定（由系统自动判定）

# ---- 航线元数据 ----
string route_id               # 航线编号（如 "WH-SZ-20260601-001"）
string route_type             # 航线类型: "transit"/"approach"/"departure"/"patrol"
