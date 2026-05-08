from m5_tactical_planner.mpc_params import MID_MPC_HORIZON_S, MID_MPC_N, MID_MPC_T_STEP


def test_mid_mpc_n_is_18():
    assert MID_MPC_N == 18


def test_mid_mpc_t_step_is_5():
    assert MID_MPC_T_STEP == 5.0


def test_mid_mpc_horizon_is_90():
    assert MID_MPC_HORIZON_S == 90.0


def test_horizon_equals_n_times_t_step():
    """Consistency invariant: N * T_step == horizon_s."""
    assert MID_MPC_N * MID_MPC_T_STEP == MID_MPC_HORIZON_S
