"""Pure guidance helpers for the SIL L4 adapter."""

from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Optional, Sequence


MAX_RUDDER_DEG = 35.0
MAX_RUDDER_RAD = math.radians(MAX_RUDDER_DEG)
MAX_SPEED_KN = 25.0
CRUISE_SPEED_KN = 10.0
SHIP_LENGTH_M = 46.0
RUDDER_SIGN = -1
M4_AVOID_TARGET_LOCK_DELTA_DEG = 150.0
M5_AVOID_WAYPOINT_MAX_DELTA_DEG = 170.0
M5_AVOID_WAYPOINT_MIN_LOOKAHEAD_M = 25.0
AVOIDANCE_CORRIDOR_SOFT_XTE_M = 180.0
AVOIDANCE_CORRIDOR_HARD_XTE_M = 280.0
AVOIDANCE_CORRIDOR_MIN_OUTBOUND_DELTA_DEG = 0.0
AVOIDANCE_CORRIDOR_SPEED_SOFT_XTE_M = 120.0
AVOIDANCE_CORRIDOR_SPEED_HARD_XTE_M = 260.0
AVOIDANCE_CORRIDOR_MIN_SPEED_KN = 0.0
FALLBACK_ROUTE_SEGMENT_M = 50000.0
TRANSIT_RETURN_SOFT_XTE_M = 50.0
TRANSIT_RETURN_HARD_XTE_M = 350.0
TRANSIT_RETURN_MIN_CORRECTION_DEG = 30.0
TRANSIT_RETURN_MAX_CORRECTION_DEG = 90.0
TRANSIT_RETURN_SOFT_SPEED_KN = 10.0
TRANSIT_RETURN_HARD_SPEED_KN = 8.0


@dataclass
class ActuatorCommand:
    rudder_angle: float = 0.0
    throttle: float = 0.0


class HeadingController:
    def __init__(self, Kp: float = 1.0, Kd: float = 0.0, max_rate_deg_s: float = 5.0):
        self.Kp = Kp
        self.Kd = Kd
        self.max_rate_deg_s = max_rate_deg_s
        self.last_cmd_deg = 0.0

    def step(
        self,
        error_deg: float,
        dt: float,
        current_rot_deg_s: float = 0.0,
    ) -> float:
        # PD on heading: the proportional term drives toward the target, the
        # derivative term on rate-of-turn damps the approach. Without the D
        # term the controller only reacts once the heading error crosses zero,
        # by which time the hull is still rotating, so it overshoots and reverses
        # -- the succession of small sign-flipping rudder kicks that COLREGs
        # Rule 8(b) forbids and the phase gate flags as small_runs. Kd defaults
        # to 0 so all existing callers (which never brake) are unchanged.
        error_deg = signed_heading_delta_deg(error_deg, 0.0)
        cmd_deg = self.Kp * error_deg - self.Kd * current_rot_deg_s
        cmd_deg = max(-MAX_RUDDER_DEG, min(MAX_RUDDER_DEG, cmd_deg))
        max_delta = self.max_rate_deg_s * dt
        cmd_deg = max(self.last_cmd_deg - max_delta,
                      min(self.last_cmd_deg + max_delta, cmd_deg))
        self.last_cmd_deg = cmd_deg
        return math.radians(cmd_deg)


class SpeedController:
    def __init__(self, Kp: float = 0.15, Ki: float = 0.02, max_rate: float = 0.5):
        self.Kp = Kp
        self.Ki = Ki
        self.max_rate = max_rate
        self.integral = 0.0
        self.last_cmd = 0.0

    def step(self, error_kn: float, dt: float) -> float:
        p_term = self.Kp * error_kn
        self.integral += error_kn * dt
        self.integral = max(-5.0, min(5.0, self.integral))
        cmd = p_term + self.Ki * self.integral
        max_delta = self.max_rate * dt
        cmd = max(self.last_cmd - max_delta, min(self.last_cmd + max_delta, cmd))
        cmd = max(0.0, min(1.0, cmd))
        self.last_cmd = cmd
        return cmd


def signed_heading_delta_deg(heading_deg: float, reference_deg: float) -> float:
    return (float(heading_deg) - float(reference_deg) + 180.0) % 360.0 - 180.0


def m4_colregs_window_target_deg(
    heading_min_deg: float,
    heading_max_deg: float,
    nominal_heading_deg: float,
) -> Optional[float]:
    h_min = float(heading_min_deg)
    h_max = float(heading_max_deg)
    if h_max < h_min:
        h_max += 360.0
    h_span = h_max - h_min
    if h_span > 300.0:
        return None

    reversal_boundary = float(nominal_heading_deg) + 180.0
    while reversal_boundary < h_min:
        reversal_boundary += 360.0
    while reversal_boundary > h_max:
        reversal_boundary -= 360.0
    if h_min < reversal_boundary < h_max:
        return h_min % 360.0
    return (h_min + (5.0 / 6.0) * h_span) % 360.0


def should_refresh_m4_colregs_target(
    current_target_deg: Optional[float],
    nominal_heading_deg: float,
    candidate_target_deg: Optional[float] = None,
) -> bool:
    if current_target_deg is None:
        return True
    current_signed_delta = signed_heading_delta_deg(
        current_target_deg, nominal_heading_deg)
    current_delta = abs(current_signed_delta)
    if candidate_target_deg is not None:
        candidate_signed_delta = signed_heading_delta_deg(
            candidate_target_deg, nominal_heading_deg)
        candidate_delta = abs(candidate_signed_delta)
        same_side = current_signed_delta * candidate_signed_delta >= 0.0
        if same_side and candidate_delta < current_delta:
            return True
        if (current_delta >= M4_AVOID_TARGET_LOCK_DELTA_DEG and
                current_signed_delta < 0.0 and
                candidate_signed_delta > 0.0 and
                candidate_delta < M4_AVOID_TARGET_LOCK_DELTA_DEG):
            return True
    return current_delta < M4_AVOID_TARGET_LOCK_DELTA_DEG


def great_circle_bearing(
    lat1: float,
    lon1: float,
    lat2: float,
    lon2: float,
) -> float:
    dlon = math.radians(lon2 - lon1)
    y = math.sin(dlon) * math.cos(math.radians(lat2))
    x = (math.cos(math.radians(lat1)) * math.sin(math.radians(lat2))
         - math.sin(math.radians(lat1)) * math.cos(math.radians(lat2))
         * math.cos(dlon))
    return math.degrees(math.atan2(y, x)) % 360.0


def segment_xte_and_distance_m(start_wp, end_wp, own_lat, own_lon):
    m_per_deg_lat = 111132.9
    m_per_deg_lon = 111319.9 * math.cos(math.radians(start_wp[0]))
    ax = start_wp[1] * m_per_deg_lon
    ay = start_wp[0] * m_per_deg_lat
    bx = end_wp[1] * m_per_deg_lon
    by = end_wp[0] * m_per_deg_lat
    px = own_lon * m_per_deg_lon
    py = own_lat * m_per_deg_lat
    dx, dy = bx - ax, by - ay
    seg = math.hypot(dx, dy)
    if seg < 1.0:
        return None, float("inf")
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (seg * seg)))
    closest_x = ax + t * dx
    closest_y = ay + t * dy
    distance_m = math.hypot(px - closest_x, py - closest_y)
    xte_m = (dx * (py - ay) - dy * (px - ax)) / seg
    return xte_m, distance_m


def _route_wps_with_heading_fallback(
    route_wps: Sequence[tuple[float, float]],
    target_lat: float,
    target_lon: float,
    fallback_heading_deg: Optional[float],
) -> Sequence[tuple[float, float]]:
    if len(route_wps) >= 2:
        return route_wps
    if fallback_heading_deg is None:
        return route_wps
    if abs(target_lat) <= 1e-4 and abs(target_lon) <= 1e-4:
        return route_wps

    m_per_deg_lat = 111132.9
    m_per_deg_lon = 111319.9 * math.cos(math.radians(target_lat))
    if abs(m_per_deg_lon) < 1.0:
        return route_wps

    heading_rad = math.radians(fallback_heading_deg)
    dx = math.sin(heading_rad) * FALLBACK_ROUTE_SEGMENT_M
    dy = math.cos(heading_rad) * FALLBACK_ROUTE_SEGMENT_M
    return [
        (target_lat - dy / m_per_deg_lat, target_lon - dx / m_per_deg_lon),
        (target_lat + dy / m_per_deg_lat, target_lon + dx / m_per_deg_lon),
    ]


def signed_xte_m(
    route_wps: Sequence[tuple[float, float]],
    own_lat: float,
    own_lon: float,
    target_lat: float = 0.0,
    target_lon: float = 0.0,
    fallback_heading_deg: Optional[float] = None,
) -> Optional[float]:
    route_wps = _route_wps_with_heading_fallback(
        route_wps, target_lat, target_lon, fallback_heading_deg)
    if len(route_wps) < 2:
        return None
    if abs(target_lat) > 1e-4 or abs(target_lon) > 1e-4:
        target_idx = min(
            range(len(route_wps)),
            key=lambda i: ((route_wps[i][0] - target_lat) ** 2
                           + (route_wps[i][1] - target_lon) ** 2),
        )
        if target_idx == 0:
            xte_m, _ = segment_xte_and_distance_m(
                route_wps[0], route_wps[1], own_lat, own_lon)
            return xte_m
        xte_m, _ = segment_xte_and_distance_m(
            route_wps[target_idx - 1], route_wps[target_idx],
            own_lat, own_lon)
        return xte_m

    best = None
    for idx in range(len(route_wps) - 1):
        xte_m, distance_m = segment_xte_and_distance_m(
            route_wps[idx], route_wps[idx + 1], own_lat, own_lon)
        if xte_m is None:
            continue
        if best is None or distance_m < best[1]:
            best = (xte_m, distance_m)
    return None if best is None else best[0]


def avoidance_waypoint_heading_deg(
    *,
    waypoints: Sequence[object],
    own_lat: float,
    own_lon: float,
    nominal_heading_deg: float,
    preferred_heading_deg: Optional[float],
) -> Optional[float]:
    for wp in waypoints:
        pos = getattr(wp, "position", None)
        if pos is None:
            continue
        try:
            lat = float(pos.latitude)
            lon = float(pos.longitude)
        except (TypeError, ValueError):
            continue
        if not (math.isfinite(lat) and math.isfinite(lon)):
            continue

        m_per_deg_lat = 111132.9
        m_per_deg_lon = 111319.9 * math.cos(math.radians(float(own_lat)))
        dist_m = math.hypot((lon - float(own_lon)) * m_per_deg_lon,
                            (lat - float(own_lat)) * m_per_deg_lat)
        if dist_m < M5_AVOID_WAYPOINT_MIN_LOOKAHEAD_M:
            continue

        bearing = great_circle_bearing(float(own_lat), float(own_lon), lat, lon)
        candidate_delta = signed_heading_delta_deg(bearing, nominal_heading_deg)
        if abs(candidate_delta) > M5_AVOID_WAYPOINT_MAX_DELTA_DEG:
            continue
        return bearing
    return None


def select_avoidance_heading(
    *,
    waypoint_heading_deg: Optional[float],
    avoidance_target_heading_deg: Optional[float],
    nominal_heading_deg: float,
) -> Optional[float]:
    if avoidance_target_heading_deg is None:
        return waypoint_heading_deg
    if waypoint_heading_deg is None:
        return avoidance_target_heading_deg

    waypoint_delta = signed_heading_delta_deg(waypoint_heading_deg, nominal_heading_deg)
    target_delta = signed_heading_delta_deg(avoidance_target_heading_deg, nominal_heading_deg)
    same_side = waypoint_delta * target_delta >= 0.0
    if not same_side:
        return avoidance_target_heading_deg
    return waypoint_heading_deg


def corridor_guarded_avoidance_heading_deg(
    *,
    selected_heading_deg: Optional[float],
    nominal_heading_deg: float,
    route_wps: Sequence[tuple[float, float]],
    own_lat: float,
    own_lon: float,
    current_target_wp_lat: float = 0.0,
    current_target_wp_lon: float = 0.0,
    soft_xte_m: float = AVOIDANCE_CORRIDOR_SOFT_XTE_M,
    hard_xte_m: float = AVOIDANCE_CORRIDOR_HARD_XTE_M,
    min_outbound_delta_deg: float = AVOIDANCE_CORRIDOR_MIN_OUTBOUND_DELTA_DEG,
) -> Optional[float]:
    if selected_heading_deg is None:
        return None
    xte_m = signed_xte_m(
        route_wps,
        own_lat,
        own_lon,
        current_target_wp_lat,
        current_target_wp_lon,
        fallback_heading_deg=nominal_heading_deg,
    )
    if xte_m is None or not math.isfinite(xte_m):
        return selected_heading_deg
    abs_xte_m = abs(xte_m)
    if abs_xte_m <= soft_xte_m:
        return selected_heading_deg

    selected_delta = signed_heading_delta_deg(
        selected_heading_deg, nominal_heading_deg)
    if abs(selected_delta) <= min_outbound_delta_deg:
        return selected_heading_deg

    # signed_xte_m is positive on one side of the route and negative on the other.
    # A heading delta with the opposite sign keeps moving farther away from track.
    if xte_m * selected_delta >= 0.0:
        return selected_heading_deg

    span = max(1.0, hard_xte_m - soft_xte_m)
    saturation = max(0.0, min(1.0, (abs_xte_m - soft_xte_m) / span))
    allowed_abs_delta = (
        min_outbound_delta_deg +
        (abs(selected_delta) - min_outbound_delta_deg) * (1.0 - saturation)
    )
    guarded_delta = math.copysign(
        min(abs(selected_delta), allowed_abs_delta), selected_delta)
    return (nominal_heading_deg + guarded_delta) % 360.0


def corridor_guarded_avoidance_speed_kn(
    *,
    target_speed_kn: float,
    selected_heading_deg: Optional[float],
    nominal_heading_deg: float,
    route_wps: Sequence[tuple[float, float]],
    own_lat: float,
    own_lon: float,
    current_target_wp_lat: float = 0.0,
    current_target_wp_lon: float = 0.0,
    soft_xte_m: float = AVOIDANCE_CORRIDOR_SPEED_SOFT_XTE_M,
    hard_xte_m: float = AVOIDANCE_CORRIDOR_SPEED_HARD_XTE_M,
    min_speed_kn: float = AVOIDANCE_CORRIDOR_MIN_SPEED_KN,
) -> float:
    xte_m = signed_xte_m(
        route_wps,
        own_lat,
        own_lon,
        current_target_wp_lat,
        current_target_wp_lon,
        fallback_heading_deg=nominal_heading_deg,
    )
    if xte_m is None or not math.isfinite(xte_m):
        return target_speed_kn
    abs_xte_m = abs(xte_m)
    if abs_xte_m <= soft_xte_m:
        return target_speed_kn
    if selected_heading_deg is None:
        return target_speed_kn

    selected_delta = signed_heading_delta_deg(
        selected_heading_deg, nominal_heading_deg)
    if xte_m * selected_delta > 0.0:
        return target_speed_kn

    span = max(1.0, hard_xte_m - soft_xte_m)
    saturation = max(0.0, min(1.0, (abs_xte_m - soft_xte_m) / span))
    guarded_speed = (
        min_speed_kn +
        (float(target_speed_kn) - min_speed_kn) * (1.0 - saturation)
    )
    return max(min_speed_kn, min(float(target_speed_kn), guarded_speed))


def command_for_heading_speed(
    *,
    target_heading_deg: float,
    target_sog_kn: float,
    current_heading_deg: float,
    current_sog_kn: float,
    current_rot_deg_s: float,
    heading_controller: HeadingController,
    speed_controller: SpeedController,
    dt: float = 0.5,
) -> ActuatorCommand:
    heading_error_deg = signed_heading_delta_deg(target_heading_deg, current_heading_deg)
    speed_error_kn = target_sog_kn - current_sog_kn
    return ActuatorCommand(
        rudder_angle=RUDDER_SIGN * heading_controller.step(
            heading_error_deg, dt, current_rot_deg_s),
        throttle=speed_controller.step(speed_error_kn, dt),
    )


def compute_transit_command(
    *,
    current_heading_deg: float,
    current_sog_kn: float,
    current_rot_deg_s: float,
    own_lat: float,
    own_lon: float,
    target_heading_deg: float,
    target_sog_kn: float,
    current_target_wp_lat: float,
    current_target_wp_lon: float,
    route_wps: Sequence[tuple[float, float]],
    heading_controller: HeadingController,
    speed_controller: SpeedController,
    dt: float = 0.5,
) -> ActuatorCommand:
    effective_target_heading = target_heading_deg
    effective_target_sog = target_sog_kn
    if abs(current_target_wp_lat) > 1e-4 or abs(current_target_wp_lon) > 1e-4:
        waypoint_heading = great_circle_bearing(
            own_lat, own_lon, current_target_wp_lat, current_target_wp_lon)
        xte = signed_xte_m(
            route_wps,
            own_lat,
            own_lon,
            current_target_wp_lat,
            current_target_wp_lon,
            fallback_heading_deg=target_heading_deg,
        )
        if xte is None:
            xte = 0.0
        abs_xte = abs(xte)
        effective_target_heading = (
            target_heading_deg
            if abs_xte > TRANSIT_RETURN_SOFT_XTE_M
            else waypoint_heading
        )
        saturation = max(
            0.0,
            min(1.0, (abs_xte - TRANSIT_RETURN_SOFT_XTE_M) /
                max(1.0, TRANSIT_RETURN_HARD_XTE_M - TRANSIT_RETURN_SOFT_XTE_M)),
        )
        correction_limit = (
            TRANSIT_RETURN_MIN_CORRECTION_DEG +
            (TRANSIT_RETURN_MAX_CORRECTION_DEG -
             TRANSIT_RETURN_MIN_CORRECTION_DEG) * saturation
        )
        xte_correction = max(-correction_limit, min(correction_limit, xte * 0.20))
        effective_target_heading = (effective_target_heading + xte_correction) % 360.0
        if abs_xte > TRANSIT_RETURN_SOFT_XTE_M:
            speed_cap = (
                TRANSIT_RETURN_SOFT_SPEED_KN +
                (TRANSIT_RETURN_HARD_SPEED_KN -
                 TRANSIT_RETURN_SOFT_SPEED_KN) * saturation
            )
            effective_target_sog = min(effective_target_sog, speed_cap)

    return command_for_heading_speed(
        target_heading_deg=effective_target_heading,
        target_sog_kn=effective_target_sog,
        current_heading_deg=current_heading_deg,
        current_sog_kn=current_sog_kn,
        current_rot_deg_s=current_rot_deg_s,
        heading_controller=heading_controller,
        speed_controller=speed_controller,
        dt=dt,
    )


def compute_avoidance_command(
    *,
    current_heading_deg: float,
    current_sog_kn: float,
    current_rot_deg_s: float,
    waypoint_heading_deg: Optional[float],
    avoidance_target_heading_deg: Optional[float],
    last_avoidance_waypoint: Optional[object],
    heading_controller: HeadingController,
    speed_controller: SpeedController,
    target_speed_override_kn: Optional[float] = None,
    dt: float = 0.5,
) -> ActuatorCommand:
    out = ActuatorCommand()
    if waypoint_heading_deg is not None:
        heading_error_deg = signed_heading_delta_deg(
            waypoint_heading_deg, current_heading_deg)
        out.rudder_angle = RUDDER_SIGN * heading_controller.step(
            heading_error_deg, dt, current_rot_deg_s)
    elif avoidance_target_heading_deg is not None:
        heading_error_deg = signed_heading_delta_deg(
            avoidance_target_heading_deg, current_heading_deg)
        out.rudder_angle = RUDDER_SIGN * heading_controller.step(
            heading_error_deg, dt, current_rot_deg_s)
    elif last_avoidance_waypoint is not None:
        turn_radius_m = float(getattr(last_avoidance_waypoint, "turn_radius_m", 0.0))
        if abs(turn_radius_m) > 1e-6:
            radius = max(abs(turn_radius_m), 50.0)
            rudder_rad = math.atan2(SHIP_LENGTH_M, radius)
            out.rudder_angle = RUDDER_SIGN * max(
                -MAX_RUDDER_RAD, min(MAX_RUDDER_RAD, rudder_rad))

    if last_avoidance_waypoint is not None:
        target_sog_kn = (
            float(target_speed_override_kn)
            if target_speed_override_kn is not None
            else float(getattr(last_avoidance_waypoint, "target_speed_kn", 0.0))
        )
        feedforward = max(0.0, min(1.0, target_sog_kn / MAX_SPEED_KN))
        speed_error_kn = target_sog_kn - current_sog_kn
        out.throttle = max(feedforward, speed_controller.step(speed_error_kn, dt))
    else:
        out.throttle = CRUISE_SPEED_KN / MAX_SPEED_KN
    return out


def safety_gate_command(active: bool) -> Optional[ActuatorCommand]:
    if not active:
        return None
    return ActuatorCommand(rudder_angle=0.0, throttle=0.0)
