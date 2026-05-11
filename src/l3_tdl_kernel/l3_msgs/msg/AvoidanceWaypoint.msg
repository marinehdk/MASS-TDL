# Avoidance path waypoint per v1.1.2 §10 + RFC-001 方案 B
geographic_msgs/GeoPoint position
float64 wp_distance_m                    # 距前一 WP 的距离
float64 safety_corridor_m                # 此段允许的横向偏差
float64 target_speed_kn                  # 此 WP 处的目标速度
float64 turn_radius_m                    # 转弯半径（速度相关的关键特性）
