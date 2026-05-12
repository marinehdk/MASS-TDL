"""Sensor Mock Node — AIS transponder + Radar model with noise injection."""
import math
import random


class SensorMockNode:
    def __init__(self, ais_drop_pct: float = 0.0, radar_max_range: float = 12000.0):
        self.ais_drop_pct = ais_drop_pct
        self.radar_max_range = radar_max_range

    def generate_ais(self, own_lat: float, own_lon: float, target: dict) -> dict | None:
        """Generate AIS message. Returns None if dropped."""
        if random.random() < self.ais_drop_pct / 100.0:
            return None
        return {
            "mmsi": target["mmsi"],
            "sog": target["sog"],
            "cog": target["cog"],
            "lat": target["lat"],
            "lon": target["lon"],
            "heading": target["heading"],
            "dropout_flag": False,
        }

    def generate_radar(self, own_lat: float, own_lon: float, targets: list[dict]) -> dict:
        """Generate radar measurement with polar targets."""
        polar = []
        for t in targets:
            dlat = (t["lat"] - own_lat) * 111120.0
            dlon = (t["lon"] - own_lon) * 111120.0 * math.cos(math.radians(own_lat))
            rng = math.sqrt(dlat**2 + dlon**2)
            if rng <= self.radar_max_range:
                bearing = math.atan2(dlon, dlat)
                polar.append({
                    "range": rng + random.gauss(0, 3.0),
                    "bearing": bearing + random.gauss(0, 0.005),
                    "rcs": random.uniform(10, 50),
                })
        return {"polar_targets": polar, "clutter_cardinality": random.randint(0, 5)}


def main():
    print("SensorMockNode ready")
