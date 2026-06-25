# Vessel parameters for wind, wave and current calculations
float64 lpp           # Length between perpendiculars (m)
float64 b             # Ship breadth (m)
float64 af            # Forward projected area (m²)
float64 al            # Lateral projected area (m²)
float64 t             # Draft (m)
float64 d             # Water depth (m)
float64 anemometer_height  # Anemometer installation height (m)
float64 z_center      # Hull force center height (m)
float64 bow_angle_rad # Bow angle (rad)
float64 los           # Waterline length (m)
float64 c_wl_aft      # Waterline coefficient at aft (dimensionless)
float64 x_los         # Waterline length center position (m)
# ── 横摇/稳性参数（新增 4-DOF）──────────────────────────────────────────────
float64 displacement_ton  # Displacement tonnage (ton) - for RAO calculation
float64 kg                # Height of center of gravity above keel (m)
float64 gm_t              # Transverse metacentric height (m)