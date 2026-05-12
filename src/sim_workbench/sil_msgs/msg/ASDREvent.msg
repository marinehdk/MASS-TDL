# ASDREvent — ASDR decision log entry from asdr_logger_node
# Mirrors sil.ASDREvent proto
# Verdict enum flattened as uint8

builtin_interfaces/Time stamp
string event_type
string rule_ref
string decision_id

# Verdict: 0=UNSPECIFIED 1=PASS 2=RISK 3=FAIL
uint8 verdict
string payload_json
