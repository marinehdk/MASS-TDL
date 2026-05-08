from typing import Final

# Mid-MPC planning horizon parameters (canonical values — do not override in module code)
# Architecture §10.3: N=18 steps × 5s = 90s lookahead
MID_MPC_N: Final[int] = 18
MID_MPC_T_STEP: Final[float] = 5.0   # seconds per step
MID_MPC_HORIZON_S: Final[float] = 90.0  # total horizon in seconds
