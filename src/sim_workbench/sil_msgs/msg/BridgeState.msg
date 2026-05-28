# Bridge state message for Foxglove visualization
builtin_interfaces/Time stamp

string latch_state            # "latched", "releasing", "released"
float32 target_heading_deg
float32 release_progress      # [0.0, 1.0]
float32 current_offset_deg
