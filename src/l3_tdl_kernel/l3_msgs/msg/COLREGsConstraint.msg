# Per v1.1.3-stub §15.1 COLREGs_ConstraintMsg
uint16 schema_version  # 115 = v1.1.3 role-carry + M6 past-clear semantics
builtin_interfaces/Time stamp

l3_msgs/RuleActive[] active_rules        # Rule 8/13/14/15/16/17 ...
string phase                             # "T_standOn" | "T_act" | "T_postAvoid"
uint8 primary_role                       # dominant target role (same enum as RuleActive.role)
string primary_preferred_direction       # dominant required action: STARBOARD|PORT|REDUCE_SPEED|HOLD
l3_msgs/Constraint[] constraints

bool conflict_detected                   # true when active_rules contain an unsolvable conflict

float32 confidence
string rationale

# D2.4 NEW: 5-layer decision rationale chain
ColregsChainLayer[] colregs_chain
string colregs_chain_target_id           # MMSI of primary target

# M6-owned COLREGs encounter lifecycle semantics for M5 TailBuilder / M7 checker.
# M5 must consume these fields and must not rejudge side/role/past-clear.
uint8 ENCOUNTER_CLEAR=0
uint8 ENCOUNTER_ONSET=1
uint8 ENCOUNTER_ACTIVE=2
uint8 ENCOUNTER_RELEASE=3
uint8 encounter_state
bool past_clear
bool release_predicted
