# Append python paths for shell_b_harness to PYTHONPATH
ament_prepend_unique_value PYTHONPATH "$COLCON_CURRENT_PREFIX/local/lib/python3.10/dist-packages"
ament_prepend_unique_value PYTHONPATH "$COLCON_CURRENT_PREFIX/lib/python3.10/site-packages"
