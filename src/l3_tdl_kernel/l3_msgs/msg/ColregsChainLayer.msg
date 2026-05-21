# D2.4: One layer of the 5-layer COLREGs decision rationale chain.
# schema_version 113 (D2.4 patch).
uint8 layer             # 1-5
string label            # human-readable layer name (UTF-8)
string conclusion       # layer decision result
string[] input_keys     # key-value pairs carrying relevant geometry
string[] input_vals     # parallel to input_keys
float32 confidence      # [0, 1]
uint8 timing_stage      # 0=N/A 1=STAGE_1 2=STAGE_2 3=STAGE_3 4=EMERGENCY
bool escalation         # true when stand-on enters SOUND_WARNING or INDEPENDENT_ACTION
string rationale        # one-line human-readable reasoning
