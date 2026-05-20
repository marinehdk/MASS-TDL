uint16 schema_version  # 112 = v1.1.2
builtin_interfaces/Time stamp
float32 confidence  # [0, 1] — 此 WP 的路径规划置信度
string rationale    # SAT-2 摘要

# Avoidance path waypoint per v1.1.2 §10 + RFC-001 方案 B
geographic_msgs/GeoPoint position
float64 wp_distance_m                    # 距前一 WP 的距离
float64 safety_corridor_m                # 此段允许的横向偏差
float64 target_speed_kn                  # 此 WP 处的目标速度
float64 turn_radius_m                    # 转弯半径（速度相关的关键特性）
