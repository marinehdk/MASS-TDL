# ENC-derived zone constraints per v1.1.2 §6.4
string zone_type                         # "open_water" | "narrow_channel" | "tss" | "harbor" | "anchorage"
bool in_tss                              # 是否处于分道通航制（Rule 10）
bool in_narrow_channel                   # 是否处于狭水道（Rule 9）
geographic_msgs/GeoPath[] tss_lanes      # TSS 通航带几何（如适用）
float32 min_water_depth_m                # 当前位置水深下限
float64[] exclusion_zones                # GeoJSON Polygon 序列化（lat/lon 对）
