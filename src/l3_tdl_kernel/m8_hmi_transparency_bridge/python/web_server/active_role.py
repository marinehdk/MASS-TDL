from enum import Enum


class ActiveRole(Enum):
    PRIMARY_ON_BOARD = "PRIMARY_ON_BOARD"
    PRIMARY_ROC = "PRIMARY_ROC"
    DUAL_OBSERVATION = "DUAL_OBSERVATION"


class TransitionError(Exception):
    pass


_VALID_TRANSITIONS: dict[ActiveRole, set[ActiveRole]] = {
    ActiveRole.PRIMARY_ON_BOARD: {ActiveRole.PRIMARY_ROC, ActiveRole.DUAL_OBSERVATION},
    ActiveRole.PRIMARY_ROC: {ActiveRole.PRIMARY_ON_BOARD, ActiveRole.DUAL_OBSERVATION},
    ActiveRole.DUAL_OBSERVATION: {ActiveRole.PRIMARY_ON_BOARD, ActiveRole.PRIMARY_ROC},
}

# Transitions involving DUAL_OBSERVATION require two distinct acknowledgers
_DUAL_ACK_TRANSITIONS: frozenset[tuple[ActiveRole, ActiveRole]] = frozenset({
    (ActiveRole.PRIMARY_ON_BOARD, ActiveRole.DUAL_OBSERVATION),
    (ActiveRole.PRIMARY_ROC, ActiveRole.DUAL_OBSERVATION),
    (ActiveRole.DUAL_OBSERVATION, ActiveRole.PRIMARY_ON_BOARD),
    (ActiveRole.DUAL_OBSERVATION, ActiveRole.PRIMARY_ROC),
})


class ActiveRoleStateMachine:
    def __init__(self, initial_role: ActiveRole) -> None:
        self._role = initial_role
        self._pending_target: ActiveRole | None = None
        self._first_acker: str | None = None

    @property
    def role(self) -> ActiveRole:
        return self._role

    def request_transition(self, target: ActiveRole, acker: str) -> ActiveRole:
        """
        Request a role transition.

        For dual-ack transitions: first call registers ack (role unchanged),
        second call with a different acker completes the transition.
        """
        if target not in _VALID_TRANSITIONS.get(self._role, set()):
            raise TransitionError(
                f"Invalid transition from {self._role.value} to {target.value}"
            )

        if (self._role, target) in _DUAL_ACK_TRANSITIONS:
            if self._pending_target != target or self._first_acker is None:
                self._pending_target = target
                self._first_acker = acker
                return self._role
            else:
                self._role = target
                self._pending_target = None
                self._first_acker = None
                return self._role
        else:
            self._role = target
            self._pending_target = None
            self._first_acker = None
            return self._role
