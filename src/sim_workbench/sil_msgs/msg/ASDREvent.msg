# ASDREvent — ASDR decision log entry
# Mirrors sil.ASDREvent proto (fields 1-6 unchanged, field 7 D3.7 addition)
# Verdict enum flattened as uint8: 0=UNSPECIFIED 1=PASS 2=RISK 3=FAIL

builtin_interfaces/Time stamp
string event_type
string rule_ref
string decision_id
uint8 verdict
string payload_json
uint8[] hmac_sha256
