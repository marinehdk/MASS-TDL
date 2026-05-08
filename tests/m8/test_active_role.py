import pytest

from m8_hmi_bridge.active_role import ActiveRole, ActiveRoleStateMachine, TransitionError

# ── 6 legal transitions ───────────────────────────────────────────────────────

def test_primary_ob_to_roc_direct():
    """PRIMARY_ON_BOARD → PRIMARY_ROC requires single request."""
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ON_BOARD)
    result = sm.request_transition(ActiveRole.PRIMARY_ROC, "bridge_officer")
    assert result == ActiveRole.PRIMARY_ROC


def test_roc_to_primary_ob_direct():
    """PRIMARY_ROC → PRIMARY_ON_BOARD requires single request."""
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ROC)
    result = sm.request_transition(ActiveRole.PRIMARY_ON_BOARD, "roc_officer")
    assert result == ActiveRole.PRIMARY_ON_BOARD


def test_primary_ob_to_dual_obs_requires_two_acks():
    """Entering DUAL_OBSERVATION from PRIMARY_ON_BOARD needs two distinct ackers."""
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ON_BOARD)
    r1 = sm.request_transition(ActiveRole.DUAL_OBSERVATION, "bridge_officer")
    assert r1 == ActiveRole.PRIMARY_ON_BOARD  # first ack — not yet transitioned
    r2 = sm.request_transition(ActiveRole.DUAL_OBSERVATION, "roc_officer")
    assert r2 == ActiveRole.DUAL_OBSERVATION


def test_roc_to_dual_obs_requires_two_acks():
    """Entering DUAL_OBSERVATION from PRIMARY_ROC needs two distinct ackers."""
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ROC)
    r1 = sm.request_transition(ActiveRole.DUAL_OBSERVATION, "roc_officer")
    assert r1 == ActiveRole.PRIMARY_ROC
    r2 = sm.request_transition(ActiveRole.DUAL_OBSERVATION, "bridge_officer")
    assert r2 == ActiveRole.DUAL_OBSERVATION


def test_dual_obs_to_primary_ob_requires_two_acks():
    """Exiting DUAL_OBSERVATION to PRIMARY_ON_BOARD needs two distinct ackers."""
    sm = ActiveRoleStateMachine(ActiveRole.DUAL_OBSERVATION)
    r1 = sm.request_transition(ActiveRole.PRIMARY_ON_BOARD, "bridge_officer")
    assert r1 == ActiveRole.DUAL_OBSERVATION
    r2 = sm.request_transition(ActiveRole.PRIMARY_ON_BOARD, "roc_officer")
    assert r2 == ActiveRole.PRIMARY_ON_BOARD


def test_dual_obs_to_roc_requires_two_acks():
    """Exiting DUAL_OBSERVATION to PRIMARY_ROC needs two distinct ackers."""
    sm = ActiveRoleStateMachine(ActiveRole.DUAL_OBSERVATION)
    r1 = sm.request_transition(ActiveRole.PRIMARY_ROC, "roc_officer")
    assert r1 == ActiveRole.DUAL_OBSERVATION
    r2 = sm.request_transition(ActiveRole.PRIMARY_ROC, "bridge_officer")
    assert r2 == ActiveRole.PRIMARY_ROC


# ── 4 illegal rejections ──────────────────────────────────────────────────────

def test_invalid_self_transition_primary_ob():
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ON_BOARD)
    with pytest.raises(TransitionError):
        sm.request_transition(ActiveRole.PRIMARY_ON_BOARD, "someone")


def test_invalid_self_transition_roc():
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ROC)
    with pytest.raises(TransitionError):
        sm.request_transition(ActiveRole.PRIMARY_ROC, "someone")


def test_invalid_self_transition_dual_obs():
    sm = ActiveRoleStateMachine(ActiveRole.DUAL_OBSERVATION)
    with pytest.raises(TransitionError):
        sm.request_transition(ActiveRole.DUAL_OBSERVATION, "someone")


def test_single_ack_to_dual_obs_is_insufficient():
    """One ack to DUAL_OBSERVATION does not transition — stays at current role."""
    sm = ActiveRoleStateMachine(ActiveRole.PRIMARY_ON_BOARD)
    result = sm.request_transition(ActiveRole.DUAL_OBSERVATION, "only_one")
    assert result == ActiveRole.PRIMARY_ON_BOARD
