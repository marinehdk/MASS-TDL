# Own ship state from latest FilteredOwnShipState snapshot per v1.1.2 §15.1
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp
geographic_msgs/GeoPoint position
float64 sog_kn                           # 对地速度（用于 CPA 计算）
float64 cog_deg                          # 对地航向
float64 heading_deg                      # 真航向
float64 u_water                          # 对水纵速 (m/s)
float64 v_water                          # 对水横速 (m/s)
float64 r_dot_deg_s                      # 转向率 (deg/s)
float64 current_speed_kn                 # 海流速度估计
float64 current_direction_deg            # 海流方向估计
float64[36] covariance                   # 6x6 row-major (pos x3, vel x3)
string nav_mode                          # "OPTIMAL" | "DR_SHORT" | "DR_LONG" | "DEGRADED"（RFC-002）
