from dataclasses import dataclass, field
from enum import Enum
from math import atan2, cos, degrees, exp, hypot, sin


_EPSILON = 1.0e-6


class ColregsDuty(str, Enum):
    FREE = "Free"
    STAND_ON_HOLD = "StandOnHold"
    GIVE_WAY = "GiveWay"
    BOTH_GIVE_WAY = "BothGiveWay"
    RULE17_ACTION = "Rule17Action"


class RiskPhase(str, Enum):
    CLEAR = "Clear"
    MONITOR = "Monitor"
    WARNING = "Warning"
    DANGER = "Danger"
    CRITICAL = "Critical"


@dataclass(frozen=True)
class OwnShipInput:
    x_m: float = 0.0
    y_m: float = 0.0
    heading_rad: float = 0.0
    sog_mps: float = 0.0
    loa_m: float = 46.0
    confidence: float = 1.0
    odd_degraded: bool = False


@dataclass(frozen=True)
class TargetInput:
    id: str = ""
    x_m: float = 0.0
    y_m: float = 0.0
    cog_rad: float = 0.0
    sog_mps: float = 0.0
    cpa_m: float = 0.0
    tcpa_s: float = 0.0
    confidence: float = 1.0


@dataclass
class DomainAxes:
    forward_m: float = 0.0
    astern_m: float = 0.0
    starboard_m: float = 0.0
    port_m: float = 0.0


@dataclass
class DomainConfig:
    superellipse_power: float = 2.5
    warning_scale: float = 1.8
    action_horizon_s: float = 600.0
    emergency_horizon_s: float = 180.0
    critical_horizon_s: float = 60.0


@dataclass
class RiskVector:
    target_id: str = ""
    range_m: float = 0.0
    relative_bearing_deg: float = 0.0
    closing_speed_mps: float = 0.0
    dcpa_m: float = 0.0
    tcpa_s: float = 0.0
    warning_margin_m: float = 0.0
    danger_margin_m: float = 0.0
    warning_ddv: float = 0.0
    danger_ddv: float = 0.0
    tdv_warning_s: float = 0.0
    tdv_danger_s: float = 0.0
    tde_warning_s: float = 0.0
    tde_danger_s: float = 0.0
    colregs_duty: ColregsDuty = ColregsDuty.FREE
    risk_phase: RiskPhase = RiskPhase.CLEAR
    risk_score: float = 0.0


@dataclass
class RunRiskSummary:
    primary_threat_id: str = ""
    primary_threat_switches: int = 0
    max_risk_score: float = 0.0
    worst_warning_margin_m: float = 0.0
    worst_danger_margin_m: float = 0.0
    max_warning_ddv: float = 0.0
    max_danger_ddv: float = 0.0
    warning_domain_exposure_s: float = 0.0
    danger_domain_exposure_s: float = 0.0
    encounter_complexity_score: float = 0.0
    risks: list[RiskVector] = field(default_factory=list)


@dataclass
class RankingState:
    previous_primary_id: str = ""
    candidate_primary_id: str = ""
    candidate_count: int = 0
    has_previous_primary: bool = False
    has_candidate_primary: bool = False


@dataclass
class RankingConfig:
    switch_score_gap: float = 0.12
    switch_confirm_samples: int = 2


def _clamp01(value: float) -> float:
    return min(max(value, 0.0), 1.0)


def _lower_bound(value: float, minimum: float) -> float:
    return value if value >= minimum else minimum


def _non_negative(value: float) -> float:
    return value if value > 0.0 else 0.0


def _sanitize_config(config: DomainConfig) -> DomainConfig:
    return DomainConfig(
        superellipse_power=_lower_bound(config.superellipse_power, 1.0),
        warning_scale=_lower_bound(config.warning_scale, 1.0),
        action_horizon_s=_non_negative(config.action_horizon_s),
        emergency_horizon_s=_non_negative(config.emergency_horizon_s),
        critical_horizon_s=_non_negative(config.critical_horizon_s),
    )


def _normalize_degrees(value: float) -> float:
    while value > 180.0:
        value -= 360.0
    while value < -180.0:
        value += 360.0
    return value


def _selected_longitudinal_axis(x_body_m: float, axes: DomainAxes) -> float:
    return axes.forward_m if x_body_m >= 0.0 else axes.astern_m


def _selected_lateral_axis(y_body_m: float, axes: DomainAxes) -> float:
    return axes.starboard_m if y_body_m >= 0.0 else axes.port_m


def _superellipse_norm(x_body_m: float, y_body_m: float, axes: DomainAxes, power: float) -> float:
    a = max(_selected_longitudinal_axis(x_body_m, axes), _EPSILON)
    b = max(_selected_lateral_axis(y_body_m, axes), _EPSILON)
    p = _lower_bound(power, 1.0)
    return (abs(x_body_m / a) ** p + abs(y_body_m / b) ** p) ** (1.0 / p)


def _boundary_margin(
    range_m: float,
    norm: float,
    x_body_m: float,
    y_body_m: float,
    axes: DomainAxes,
) -> float:
    if range_m <= _EPSILON and norm <= _EPSILON:
        longitudinal_axis_m = max(_selected_longitudinal_axis(x_body_m, axes), _EPSILON)
        lateral_axis_m = max(_selected_lateral_axis(y_body_m, axes), _EPSILON)
        return -min(longitudinal_axis_m, lateral_axis_m)
    boundary_range_m = range_m / max(norm, _EPSILON)
    return range_m - boundary_range_m


def _time_to_violation(margin_m: float, closing_speed_mps: float) -> float:
    if margin_m <= 0.0:
        return 0.0
    if closing_speed_mps > _EPSILON:
        return margin_m / closing_speed_mps
    return 0.0


def _time_to_exit(margin_m: float, closing_speed_mps: float) -> float:
    if margin_m >= 0.0 or closing_speed_mps >= -_EPSILON:
        return 0.0
    return -margin_m / -closing_speed_mps


def _colregs_score_component(duty: ColregsDuty) -> float:
    if duty in (ColregsDuty.GIVE_WAY, ColregsDuty.BOTH_GIVE_WAY):
        return 1.0
    if duty == ColregsDuty.RULE17_ACTION:
        return 0.6
    if duty == ColregsDuty.STAND_ON_HOLD:
        return 0.3
    return 0.0


def _phase_rank(phase: RiskPhase) -> int:
    return {
        RiskPhase.CLEAR: 0,
        RiskPhase.MONITOR: 1,
        RiskPhase.WARNING: 2,
        RiskPhase.DANGER: 3,
        RiskPhase.CRITICAL: 4,
    }[phase]


def _duty_rank(duty: ColregsDuty) -> int:
    return {
        ColregsDuty.FREE: 0,
        ColregsDuty.STAND_ON_HOLD: 1,
        ColregsDuty.GIVE_WAY: 2,
        ColregsDuty.BOTH_GIVE_WAY: 3,
        ColregsDuty.RULE17_ACTION: 4,
    }[duty]


def _has_non_negative_tcpa(risk: RiskVector) -> bool:
    return risk.tcpa_s >= 0.0


def _compare_smaller(candidate: float, current: float) -> int:
    if abs(candidate - current) <= _EPSILON:
        return 0
    return 1 if candidate < current else -1


def _compare_larger(candidate: float, current: float) -> int:
    if abs(candidate - current) <= _EPSILON:
        return 0
    return 1 if candidate > current else -1


def _compare_shorter_non_negative(candidate: float, current: float) -> int:
    candidate_valid = candidate >= 0.0
    current_valid = current >= 0.0
    if candidate_valid != current_valid:
        return 1 if candidate_valid else -1
    return _compare_smaller(candidate, current)


def _compare_centerline_bearing(candidate: float, current: float) -> int:
    abs_order = _compare_smaller(abs(candidate), abs(current))
    if abs_order != 0:
        return abs_order
    return _compare_smaller(candidate, current)


def _compare_larger_int(candidate: int, current: int) -> int:
    if candidate == current:
        return 0
    return 1 if candidate > current else -1


def _is_better_primary_candidate(candidate: RiskVector, current: RiskVector) -> bool:
    candidate_phase = _phase_rank(candidate.risk_phase)
    current_phase = _phase_rank(current.risk_phase)
    if candidate_phase != current_phase:
        return candidate_phase > current_phase

    if abs(candidate.risk_score - current.risk_score) > _EPSILON:
        return candidate.risk_score > current.risk_score

    candidate_tcpa_valid = _has_non_negative_tcpa(candidate)
    current_tcpa_valid = _has_non_negative_tcpa(current)
    if candidate_tcpa_valid != current_tcpa_valid:
        return candidate_tcpa_valid
    if candidate_tcpa_valid and abs(candidate.tcpa_s - current.tcpa_s) > _EPSILON:
        return candidate.tcpa_s < current.tcpa_s

    if abs(candidate.range_m - current.range_m) > _EPSILON:
        return candidate.range_m < current.range_m

    order = _compare_smaller(candidate.dcpa_m, current.dcpa_m)
    if order != 0:
        return order > 0
    order = _compare_centerline_bearing(
        candidate.relative_bearing_deg, current.relative_bearing_deg
    )
    if order != 0:
        return order > 0
    order = _compare_smaller(candidate.warning_margin_m, current.warning_margin_m)
    if order != 0:
        return order > 0
    order = _compare_smaller(candidate.danger_margin_m, current.danger_margin_m)
    if order != 0:
        return order > 0
    order = _compare_larger(candidate.warning_ddv, current.warning_ddv)
    if order != 0:
        return order > 0
    order = _compare_larger(candidate.danger_ddv, current.danger_ddv)
    if order != 0:
        return order > 0
    order = _compare_shorter_non_negative(candidate.tdv_warning_s, current.tdv_warning_s)
    if order != 0:
        return order > 0
    order = _compare_shorter_non_negative(candidate.tdv_danger_s, current.tdv_danger_s)
    if order != 0:
        return order > 0
    order = _compare_larger(candidate.tde_warning_s, current.tde_warning_s)
    if order != 0:
        return order > 0
    order = _compare_larger(candidate.tde_danger_s, current.tde_danger_s)
    if order != 0:
        return order > 0
    order = _compare_larger(candidate.closing_speed_mps, current.closing_speed_mps)
    if order != 0:
        return order > 0
    order = _compare_larger_int(
        _duty_rank(candidate.colregs_duty), _duty_rank(current.colregs_duty)
    )
    if order != 0:
        return order > 0
    if candidate.target_id != current.target_id:
        return candidate.target_id < current.target_id
    return False


def _find_best_by_target_id(risks: list[RiskVector], target_id: str) -> RiskVector | None:
    best: RiskVector | None = None
    for risk in risks:
        if risk.target_id != target_id:
            continue
        if best is None or _is_better_primary_candidate(risk, best):
            best = risk
    return best


def _clear_candidate(state: RankingState) -> None:
    state.candidate_primary_id = ""
    state.has_candidate_primary = False
    state.candidate_count = 0


def _clear_previous(state: RankingState) -> None:
    state.previous_primary_id = ""
    state.has_previous_primary = False


def _promote_primary(state: RankingState, risk: RiskVector) -> None:
    state.previous_primary_id = risk.target_id
    state.has_previous_primary = True
    _clear_candidate(state)


def danger_axes(own: OwnShipInput) -> DomainAxes:
    length_m = max(own.loa_m, 1.0)
    speed_mps = _non_negative(own.sog_mps)
    return DomainAxes(
        forward_m=max(8.0 * length_m, speed_mps * 60.0 + 2.0 * length_m, 300.0),
        astern_m=max(3.0 * length_m, 150.0),
        starboard_m=max(5.0 * length_m, speed_mps * 30.0 + length_m, 220.0),
        port_m=max(4.0 * length_m, speed_mps * 25.0 + length_m, 185.0),
    )


def warning_axes(own: OwnShipInput, config: DomainConfig | None = None) -> DomainAxes:
    safe_config = _sanitize_config(config if config is not None else DomainConfig())
    danger = danger_axes(own)
    return DomainAxes(
        forward_m=danger.forward_m * safe_config.warning_scale,
        astern_m=danger.astern_m * safe_config.warning_scale,
        starboard_m=danger.starboard_m * safe_config.warning_scale,
        port_m=danger.port_m * safe_config.warning_scale,
    )


def evaluate_target(
    own: OwnShipInput,
    target: TargetInput,
    duty: ColregsDuty,
    config: DomainConfig | None = None,
) -> RiskVector:
    active_config = config if config is not None else DomainConfig()
    safe_config = _sanitize_config(active_config)
    dx_m = target.x_m - own.x_m
    dy_m = target.y_m - own.y_m
    cos_heading = cos(own.heading_rad)
    sin_heading = sin(own.heading_rad)
    x_body_m = cos_heading * dx_m + sin_heading * dy_m
    y_body_m = -sin_heading * dx_m + cos_heading * dy_m
    range_m = hypot(dx_m, dy_m)

    own_speed_mps = _non_negative(own.sog_mps)
    target_speed_mps = _non_negative(target.sog_mps)
    own_vx_mps = own_speed_mps * cos(own.heading_rad)
    own_vy_mps = own_speed_mps * sin(own.heading_rad)
    target_vx_mps = target_speed_mps * cos(target.cog_rad)
    target_vy_mps = target_speed_mps * sin(target.cog_rad)
    rel_vx_mps = target_vx_mps - own_vx_mps
    rel_vy_mps = target_vy_mps - own_vy_mps
    closing_speed_mps = (
        -((rel_vx_mps * dx_m + rel_vy_mps * dy_m) / range_m) if range_m > _EPSILON else 0.0
    )

    danger = danger_axes(own)
    warning = warning_axes(own, active_config)
    danger_norm = _superellipse_norm(
        x_body_m, y_body_m, danger, safe_config.superellipse_power
    )
    warning_norm = _superellipse_norm(
        x_body_m, y_body_m, warning, safe_config.superellipse_power
    )

    risk = RiskVector(
        target_id=target.id,
        range_m=range_m,
        relative_bearing_deg=_normalize_degrees(degrees(atan2(y_body_m, x_body_m))),
        closing_speed_mps=closing_speed_mps,
        dcpa_m=target.cpa_m,
        tcpa_s=target.tcpa_s,
        warning_margin_m=_boundary_margin(range_m, warning_norm, x_body_m, y_body_m, warning),
        danger_margin_m=_boundary_margin(range_m, danger_norm, x_body_m, y_body_m, danger),
        warning_ddv=max(0.0, 1.0 - warning_norm),
        danger_ddv=max(0.0, 1.0 - danger_norm),
        colregs_duty=duty,
    )
    risk.tdv_warning_s = _time_to_violation(risk.warning_margin_m, closing_speed_mps)
    risk.tdv_danger_s = _time_to_violation(risk.danger_margin_m, closing_speed_mps)
    risk.tde_warning_s = _time_to_exit(risk.warning_margin_m, closing_speed_mps)
    risk.tde_danger_s = _time_to_exit(risk.danger_margin_m, closing_speed_mps)

    if (
        risk.danger_ddv > 0.0
        and target.tcpa_s <= safe_config.critical_horizon_s
        and target.tcpa_s >= 0.0
    ):
        risk.risk_phase = RiskPhase.CRITICAL
    elif risk.danger_ddv > 0.0 or risk.danger_margin_m < 0.0:
        risk.risk_phase = RiskPhase.DANGER
    elif risk.warning_ddv > 0.0 or risk.warning_margin_m < 0.0:
        risk.risk_phase = RiskPhase.WARNING
    elif (
        target.tcpa_s >= 0.0
        and target.tcpa_s <= safe_config.action_horizon_s
        and risk.dcpa_m < warning.forward_m
    ):
        risk.risk_phase = RiskPhase.MONITOR
    else:
        risk.risk_phase = RiskPhase.CLEAR

    domain_component = max(risk.warning_ddv * 0.7, risk.danger_ddv)
    urgency_component = exp(-target.tcpa_s / 180.0) if target.tcpa_s >= 0.0 else 0.0
    closing_component = _clamp01(closing_speed_mps / 8.0)
    uncertainty_component = 1.0 - _clamp01(target.confidence)
    risk.risk_score = _clamp01(
        0.40 * domain_component
        + 0.25 * urgency_component
        + 0.15 * closing_component
        + 0.15 * _colregs_score_component(duty)
        + 0.05 * uncertainty_component
    )
    return risk


def select_primary(
    risks: list[RiskVector],
    state: RankingState | None,
    config: RankingConfig | None = None,
) -> RiskVector:
    active_config = config if config is not None else RankingConfig()
    if not risks:
        if state is not None:
            _clear_previous(state)
            _clear_candidate(state)
        return RiskVector()

    best = risks[0]
    for risk in risks[1:]:
        if _is_better_primary_candidate(risk, best):
            best = risk

    if state is None:
        return best

    if not state.has_previous_primary:
        _promote_primary(state, best)
        return best

    previous = _find_best_by_target_id(risks, state.previous_primary_id)
    if previous is None:
        _promote_primary(state, best)
        return best

    if best.target_id == state.previous_primary_id:
        _promote_primary(state, best)
        return best

    switch_score_gap = max(0.0, active_config.switch_score_gap)
    score_gap = best.risk_score - previous.risk_score
    if (
        _phase_rank(best.risk_phase) != _phase_rank(previous.risk_phase)
        or score_gap >= switch_score_gap
    ):
        _promote_primary(state, best)
        return best

    required_samples = max(1, active_config.switch_confirm_samples)
    if state.has_candidate_primary and state.candidate_primary_id == best.target_id:
        if state.candidate_count < required_samples:
            state.candidate_count += 1
        else:
            state.candidate_count = required_samples
    else:
        state.candidate_primary_id = best.target_id
        state.has_candidate_primary = True
        state.candidate_count = 1

    if state.candidate_count >= required_samples:
        _promote_primary(state, best)
        return best
    return previous
