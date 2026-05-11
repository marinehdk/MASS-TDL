# Tracked target with classification + COLREG-relevant geometry
# Per v1.1.2 §6.2 (M2 World Model) + §15.1 World_StateMsg
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp           # 跟踪起点时间
uint64 target_id                         # 全局唯一 ID
geographic_msgs/GeoPoint position        # WGS84 (lat/lon/alt)
float64 sog_kn                           # Speed Over Ground
float64 cog_deg                          # Course Over Ground (0–360)
float64 heading_deg                      # 真航向（含偏航；雷达不一定可得）
float64[9] covariance                    # 3x3 row-major (lat/lon/heading)
string classification                    # "vessel" | "fixed_object" | "unknown"
float32 classification_confidence        # [0, 1]
float64 cpa_m                            # M2 计算的 CPA
float64 tcpa_s                           # M2 计算的 TCPA
l3_msgs/EncounterClassification encounter # COLREG 几何预分类（M2 §6.2 子模块）
float32 confidence                       # 整体置信度
string source_sensor                     # "radar" | "ais" | "lidar" | "camera" | "fused"
