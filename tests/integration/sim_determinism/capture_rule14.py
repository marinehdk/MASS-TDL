#!/usr/bin/env python3
"""Drives SIL lifecycle and captures own-ship telemetry to a CSV file.
Respects simulation time and ensures robust capturing.
"""
from __future__ import annotations
import argparse, csv, json, math, ssl, sys, time, urllib.request, urllib.error
import rclpy
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
from rosgraph_msgs.msg import Clock as ClockMsg
from sil_msgs.msg import OwnShipState, BridgeState
from l3_msgs.msg import BehaviorPlan, COLREGsConstraint

ORCHESTRATOR_URL = "https://localhost:8000"
SSL_CTX = ssl.create_default_context()
SSL_CTX.check_hostname = False
SSL_CTX.verify_mode = ssl.CERT_NONE

ROUTE_LON = 10.38
BEHAVIOR_NAMES = {"0": "TRANSIT", "1": "COLREG_AVOID", "2": "RESTRICTED_VIS", "3": "CHANNEL_FOLLOW", "4": "MRC_DRIFT"}

def _api(path, payload=None):
    data = json.dumps(payload).encode() if payload else None
    req = urllib.request.Request(path and ORCHESTRATOR_URL + path, data=data,
        headers={"Content-Type": "application/json"} if data else {}, method="POST")
    try:
        with urllib.request.urlopen(req, context=SSL_CTX, timeout=30) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="ignore")
        print(f"HTTPError on API call to {path}: {e.code} {e.reason}\nBody: {body}", file=sys.stderr, flush=True)
        raise

def _easting(lon, lat):
    return (lon - ROUTE_LON) * math.pi/180.0 * 6_371_000 * math.cos(lat*math.pi/180.0)

class Cap(Node):
    def __init__(self, dur, out):
        super().__init__("capture_rule14")
        self.dur, self.out, self.rows = dur, out, []
        self.beh, self.conf, self.sim_t = "", 0, 0.0
        self.first_received = False
        self.sim_start_t = 0.0
        self.running = True
        self.wall_start = time.time()
        self._last_actuator_rudder = 0.0
        self._last_actuator_throttle = 0.0
        self._latch_offset = 0.0
        
        q = QoSProfile(reliability=ReliabilityPolicy.BEST_EFFORT, durability=DurabilityPolicy.VOLATILE,
                       history=HistoryPolicy.KEEP_LAST, depth=10)
        self.create_subscription(OwnShipState, "/sil/own_ship_state", self._oss, q)
        self.create_subscription(BehaviorPlan, "/l3/m4/behavior_plan", self._bp, q)
        self.create_subscription(COLREGsConstraint, "/l3/m6/colregs_constraint", self._cr, q)
        self.create_subscription(ClockMsg, "/clock", self._clk, q)
        self.create_subscription(OwnShipState, "/sil/actuator_cmd", self._act, q)
        self.create_subscription(BridgeState, "/sil/bridge_state", self._bs, q)
        
    def _clk(self, m):
        self.sim_t = m.clock.sec + m.clock.nanosec * 1e-9
        
    def _bp(self, m):
        self.beh = BEHAVIOR_NAMES.get(str(m.behavior), str(m.behavior))
        
    def _cr(self, m):
        self.conf = int(m.conflict_detected)
        
    def _act(self, m):
        self._last_actuator_rudder = round(math.degrees(m.rudder_angle), 3)
        self._last_actuator_throttle = round(m.throttle, 3)

    def _bs(self, m):
        self._latch_offset = round(m.current_offset_deg, 3)
        
    def _oss(self, m):
        if not self.running:
            return
        if not self.first_received:
            self.first_received = True
            self.sim_start_t = self.sim_t
            print(f"[cap] First OwnShipState received at sim_t={self.sim_t:.2f}. Starting telemetry capture for {self.dur:.1f} sim seconds.", flush=True)
            
        self.rows.append({
            "sim_t": round(self.sim_t, 2),
            "lat": round(m.lat, 6),
            "lon": round(m.lon, 6),
            "heading_deg": round(math.degrees(m.heading), 3),
            "rudder_deg": round(math.degrees(getattr(m, "rudder_angle", 0.0)), 3),
            "sog": round(m.sog, 2),
            "rot": round(getattr(m, "rot", 0.0), 4),
            "xte_m": round(_easting(m.lon, m.lat), 2),
            "behavior": self.beh,
            "conflict": self.conf,
            "act_rudder_deg": self._last_actuator_rudder,
            "act_throttle": self._last_actuator_throttle,
            "latch_offset": self._latch_offset
        })
        
        sim_elapsed = self.sim_t - self.sim_start_t
        if sim_elapsed >= self.dur:
            print(f"[cap] Simulation duration limit reached: {sim_elapsed:.2f} >= {self.dur:.1f} s. Stopping capture.", flush=True)
            self.running = False

    def done(self):
        wall_elapsed = time.time() - self.wall_start
        if not self.first_received and wall_elapsed > 30.0:
            print("[cap] Timeout waiting for first OwnShipState message. Stopping capture.", file=sys.stderr, flush=True)
            self.running = False
        elif self.first_received and wall_elapsed > max(self.dur * 3.0, 180.0):
            print("[cap] Wall clock fallback timeout reached. Stopping capture.", file=sys.stderr, flush=True)
            self.running = False
        return not self.running

    def save(self):
        if not self.rows:
            return
        with open(self.out, "w", newline="") as f:
            wr = csv.DictWriter(f, fieldnames=list(self.rows[0].keys()))
            wr.writeheader()
            wr.writerows(self.rows)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--scenario", default="colreg-rule14-ho")
    p.add_argument("--rate", type=float, default=1.0)
    p.add_argument("--duration", type=float, default=350.0)
    p.add_argument("--output", default="/tmp/capture_rule14.csv")
    a = p.parse_args()
    
    # 0. Lifecycle cleanup to ensure no single-instance conflicts
    print(f"[cap] cleanup", flush=True)
    _api("/api/v1/lifecycle/cleanup")
    time.sleep(1.0)
    
    print(f"[cap] configure {a.scenario}", flush=True)
    _api("/api/v1/lifecycle/configure", {"scenario_id": a.scenario})
    time.sleep(2.0)
    
    print(f"[cap] rate={a.rate}", flush=True)
    _api("/api/v1/lifecycle/rate", {"rate": a.rate})
    time.sleep(0.5)
    
    print(f"[cap] activate", flush=True)
    _api("/api/v1/lifecycle/activate")
    time.sleep(0.5)
    
    rclpy.init()
    n = Cap(a.duration, a.output)
    while rclpy.ok() and not n.done():
        rclpy.spin_once(n, timeout_sec=0.1)
    
    n.save()
    print(f"[cap] {len(n.rows)} rows -> {a.output}", flush=True)
    n.destroy_node()
    rclpy.shutdown()
    
    print(f"[cap] deactivate", flush=True)
    _api("/api/v1/lifecycle/deactivate")

if __name__ == "__main__":
    main()
