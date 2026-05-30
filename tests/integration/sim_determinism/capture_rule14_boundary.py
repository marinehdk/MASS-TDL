#!/usr/bin/env python3
"""Extended SIL capture for colreg-rule14-ho boundary telemetry (AC-1…AC-7)."""
from __future__ import annotations
import argparse, csv, json, math, ssl, sys, time, urllib.request, urllib.error
import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from rosgraph_msgs.msg import Clock as ClockMsg
from sil_msgs.msg import OwnShipState, TargetVesselState
from l3_msgs.msg import BehaviorPlan, COLREGsConstraint, MissionGoal, AvoidancePlan

ORCHESTRATOR_URL = "https://localhost:8000"
SSL_CTX = ssl.create_default_context()
SSL_CTX.check_hostname = False
SSL_CTX.verify_mode = ssl.CERT_NONE
ROUTE_LON = 10.38

BEHAVIOR_NAMES = {
    "0": "TRANSIT", "1": "COLREG_AVOID", "2": "RESTRICTED_VIS",
    "3": "CHANNEL_FOLLOW", "4": "MRC_DRIFT",
}

def _api(path, payload=None):
    data = json.dumps(payload).encode() if payload else None
    req = urllib.request.Request(
        ORCHESTRATOR_URL + path, data=data,
        headers={"Content-Type": "application/json"} if data else {},
        method="POST")
    try:
        with urllib.request.urlopen(req, context=SSL_CTX, timeout=30) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="ignore")
        print(f"HTTPError {path}: {e.code} {e.reason}\n{body}", file=sys.stderr, flush=True)
        raise

def _easting(lon, lat):
    return (lon - ROUTE_LON) * math.pi / 180.0 * 6_371_000 * math.cos(lat * math.pi / 180.0)

def _haversine_m(lat1, lon1, lat2, lon2):
    """Haversine distance in metres between two lat/lon points."""
    R = 6_371_000.0
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2
         + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2))
         * math.sin(dlon / 2) ** 2)
    return 2.0 * R * math.asin(math.sqrt(a))

class CapBoundary(Node):
    def __init__(self, dur: float, out: str):
        super().__init__("capture_rule14_boundary")
        self.dur, self.out, self.rows = dur, out, []
        self.sim_t = 0.0
        self.first_received = False
        self.sim_start_t = 0.0
        self.running = True
        self.wall_start = time.time()

        # Latest boundary fields
        self._behavior = ""
        self._conflict = 0
        self._task_validity = -1
        self._ctwp_lat = 0.0
        self._ctwp_lon = 0.0
        self._avoidance_status = ""
        self._avoidance_turn_r = 0.0
        self._xte_nm = -1.0
        self._target_lat = 0.0
        self._target_lon = 0.0

        q = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST, depth=10)

        self.create_subscription(OwnShipState, "/sil/own_ship_state", self._oss, q)
        self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan", self._bp, q)
        self.create_subscription(COLREGsConstraint, "/l3/m6/colregs_constraint", self._cr, q)
        self.create_subscription(ClockMsg, "/clock", self._clk, q)
        self.create_subscription(MissionGoal, "/l3/m3/mission_goal", self._mg, q)
        self.create_subscription(AvoidancePlan, "/l3/m5/avoidance_plan", self._ap, q)
        self.create_subscription(TargetVesselState, "/sil/target_vessel_state", self._tvs, q)

    def _clk(self, m):
        self.sim_t = m.clock.sec + m.clock.nanosec * 1e-9

    def _bp(self, m):
        self._behavior = BEHAVIOR_NAMES.get(str(m.behavior), str(m.behavior))

    def _cr(self, m):
        self._conflict = int(m.conflict_detected)

    def _mg(self, m):
        self._task_validity = int(m.task_validity)
        self._ctwp_lat = float(m.current_target_wp.latitude)
        self._ctwp_lon = float(m.current_target_wp.longitude)
        self._xte_nm = float(m.xte_nm)

    def _ap(self, m):
        self._avoidance_status = str(m.status)
        if m.waypoints:
            self._avoidance_turn_r = float(m.waypoints[0].turn_radius_m)
        else:
            self._avoidance_turn_r = 0.0

    def _tvs(self, m):
        self._target_lat = float(m.lat)
        self._target_lon = float(m.lon)

    def _oss(self, m):
        if not self.running:
            return
        if not self.first_received:
            self.first_received = True
            self.sim_start_t = self.sim_t
            print(f"[cap] First OSS at sim_t={self.sim_t:.2f}. Capturing {self.dur:.1f}s", flush=True)

        # Compute haversine distance to target (0.0 if target not yet received)
        own_lat = float(m.lat)
        own_lon = float(m.lon)
        cpa_m = 0.0
        if self._target_lat != 0.0 or self._target_lon != 0.0:
            cpa_m = _haversine_m(own_lat, own_lon, self._target_lat, self._target_lon)

        self.rows.append({
            "sim_t":            round(self.sim_t, 2),
            "lat":              round(own_lat, 6),
            "lon":              round(own_lon, 6),
            "heading_deg":      round(math.degrees(m.heading), 3),
            "sog":              round(m.sog, 2),
            "xte_m":            round(_easting(own_lon, own_lat), 2),
            "behavior":         self._behavior,
            "conflict":         self._conflict,
            "task_validity":    self._task_validity,
            "ctwp_lat":         round(self._ctwp_lat, 6),
            "ctwp_lon":         round(self._ctwp_lon, 6),
            "target_lat":       round(self._target_lat, 6),
            "target_lon":       round(self._target_lon, 6),
            "cpa_m":            round(cpa_m, 1),
            "avoidance_status": self._avoidance_status,
            "avoidance_turn_r": round(self._avoidance_turn_r, 2),
            "xte_nm":           round(self._xte_nm, 4),
            "wall_t":           round(time.time() - self.wall_start, 3),
        })

        sim_elapsed = self.sim_t - self.sim_start_t
        if sim_elapsed >= self.dur:
            print(f"[cap] Done: {sim_elapsed:.2f}s captured", flush=True)
            self.running = False

    def done(self):
        wall_elapsed = time.time() - self.wall_start
        if not self.first_received and wall_elapsed > 30.0:
            print("[cap] Timeout waiting for OwnShipState", file=sys.stderr, flush=True)
            self.running = False
        elif self.first_received and wall_elapsed > max(self.dur * 3.0, 180.0):
            print("[cap] Wall-clock fallback timeout", file=sys.stderr, flush=True)
            self.running = False
        return not self.running

    def save(self):
        if not self.rows:
            return
        with open(self.out, "w", newline="") as f:
            wr = csv.DictWriter(f, fieldnames=list(self.rows[0].keys()))
            wr.writeheader()
            wr.writerows(self.rows)

def run_capture(scenario: str = "colreg-rule14-ho", rate: float = 1.0,
                duration: float = 350.0, output: str = "/tmp/cap_boundary.csv"):
    """Can be called from test code or as __main__."""
    _api("/api/v1/lifecycle/cleanup")
    time.sleep(1.0)
    _api("/api/v1/lifecycle/configure", {"scenario_id": scenario})
    time.sleep(2.0)
    _api("/api/v1/lifecycle/rate", {"rate": rate})
    time.sleep(0.5)
    _api("/api/v1/lifecycle/activate")
    time.sleep(0.5)

    rclpy.init()
    n = CapBoundary(duration, output)
    while rclpy.ok() and not n.done():
        rclpy.spin_once(n, timeout_sec=0.1)
    n.save()
    print(f"[cap] {len(n.rows)} rows → {output}", flush=True)
    n.destroy_node()
    rclpy.shutdown()

    _api("/api/v1/lifecycle/deactivate")
    return output

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--scenario", default="colreg-rule14-ho")
    p.add_argument("--rate", type=float, default=1.0)
    p.add_argument("--duration", type=float, default=350.0)
    p.add_argument("--output", default="/tmp/cap_boundary.csv")
    a = p.parse_args()
    run_capture(a.scenario, a.rate, a.duration, a.output)

if __name__ == "__main__":
    main()
