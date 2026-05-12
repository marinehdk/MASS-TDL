"""Tracker Mock Node — God tracker (perfect GT) + KF tracker."""
import math


class KalmanFilter2D:
    """Simple 4-state KF: x, y, vx, vy."""

    def __init__(self, dt: float = 0.1, process_noise: float = 0.1):
        self.dt = dt
        self.q = process_noise
        self.x = None  # [x, y, vx, vy]
        self.P = None

    def init(self, x: float, y: float, vx: float = 0.0, vy: float = 0.0):
        self.x = [x, y, vx, vy]
        self.P = [[1, 0, 0, 0],
                  [0, 1, 0, 0],
                  [0, 0, 1, 0],
                  [0, 0, 0, 1]]

    def predict(self):
        if self.x is None:
            return
        dt = self.dt
        F = [[1, 0, dt, 0],
             [0, 1, 0, dt],
             [0, 0, 1, 0],
             [0, 0, 0, 1]]
        self.x = [sum(F[i][j] * self.x[j] for j in range(4)) for i in range(4)]

    def update(self, zx: float, zy: float):
        if self.x is None:
            self.init(zx, zy)
            return
        self.x[0] = zx
        self.x[1] = zy
        # Simplified: direct measurement update (full KF in Phase 2)

    def get_state(self) -> dict:
        if self.x is None:
            return {"x": 0, "y": 0, "vx": 0, "vy": 0}
        return {"x": self.x[0], "y": self.x[1],
                "vx": self.x[2], "vy": self.x[3]}


class TrackerMockNode:
    """Tracker mock — 'god' (perfect GT passthrough) or 'kf' (KF smoothed)."""

    def __init__(self, tracker_type: str = "god"):
        self.tracker_type = tracker_type
        self._kfs: dict[int, KalmanFilter2D] = {}

    def track(self, targets: list[dict]) -> list[dict]:
        results = []
        for t in targets:
            mmsi = t["mmsi"]
            if self.tracker_type == "god":
                # God tracker: perfect passthrough
                results.append({
                    "mmsi": mmsi,
                    "lat": t["lat"],
                    "lon": t["lon"],
                    "sog": t["sog"],
                    "cog": t["cog"],
                })
            else:
                # KF tracker
                if mmsi not in self._kfs:
                    self._kfs[mmsi] = KalmanFilter2D()
                kf = self._kfs[mmsi]
                kf.predict()
                kf.update(t["lat"], t["lon"])
                s = kf.get_state()
                results.append({
                    "mmsi": mmsi,
                    "lat": s["x"],
                    "lon": s["y"],
                    "sog": math.sqrt(s["vx"] ** 2 + s["vy"] ** 2),
                    "cog": math.atan2(s["vy"], s["vx"]),
                })
        return results


def main():
    print("TrackerMockNode ready")
