# Per v1.1.2 §15.1 COLREGs_ConstraintMsg
string schema_version  # default: "v1.1.2"
builtin_interfaces/Time stamp

l3_msgs/RuleActive[] active_rules        # Rule 8/13/14/15/16/17 ...
string phase                             # "T_standOn" | "T_act" | "T_postAvoid"
l3_msgs/Constraint[] constraints

bool conflict_detected                   # true when active_rules contain an unsolvable conflict

float32 confidence
string rationale
