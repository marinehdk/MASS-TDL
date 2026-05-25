"""D3.6 adversarial RL fuzzer stub.

D3.6: random-perturbation implementation biased toward Rule boundary zones.
D4.6: replace with trained RL agent (PPO/SAC on failure trajectories).
"""
from __future__ import annotations

import csv
import math
import random
from pathlib import Path
from typing import Optional


class RLFuzzerStub:
    """Adversarial scenario generator stub.

    generate(): maritime-schema TrafficSituation dict with adversarial perturbation.
    load_failure_cases(): D4.6 hook -- seeds RL training from prior failure cases.
    """

    def __init__(self, seed: Optional[int] = None):
        self._rng = random.Random(seed)
        self._failure_bias: list[dict] = []

    def generate(
        self,
        rule: str,
        odd: str,
        seed: int,
        adversarial_intensity: float = 0.8,
    ) -> dict:
        """Return maritime-schema v3.0 TrafficSituation dict.

        Adversarial bias: push target bearing +/-5deg toward ambiguous Rule boundary;
        push CPA toward 0.27 NM threshold (boundary zone per D3.6 60:25:15 spec).
        adversarial_intensity [0.0, 1.0]: 0=nominal, 1=maximum perturbation.
        """
        self._rng.seed(seed)

        _ODD_POS = {
            "open_sea": (63.44, 10.38),
            "coastal_traffic_separation": (55.00, 10.00),
            "port_approach": (63.43, 10.40),
            "offshore_wind_farm": (56.00, 7.50),
        }
        _RULE_BEARING = {
            "Rule5": 45.0, "Rule6": 90.0, "Rule7": 90.0, "Rule8": 45.0,
            "Rule9": 0.0, "Rule13": 160.0, "Rule14": 0.0,
            "Rule15": 45.0, "Rule16": 45.0, "Rule17": 315.0, "Rule19": 90.0,
        }

        own_lat, own_lon = _ODD_POS.get(odd, (63.44, 10.38))
        canonical_bearing = _RULE_BEARING.get(rule, 45.0)
        bearing_perturb = adversarial_intensity * self._rng.uniform(-5.0, 5.0)
        bearing_deg = (canonical_bearing + bearing_perturb) % 360.0

        approach_angle_deg = 5.0 + adversarial_intensity * 5.0
        target_range_nm = max(1.0, 0.27 / max(0.01, math.sin(math.radians(approach_angle_deg))))
        target_range_nm += self._rng.uniform(-0.3, 0.3)

        bearing_rad = math.radians(bearing_deg)
        range_m = target_range_nm * 1852.0
        tgt_lat = own_lat + (range_m * math.cos(bearing_rad)) / 111_120.0
        tgt_lon = own_lon + (range_m * math.sin(bearing_rad)) / (
            111_120.0 * math.cos(math.radians(own_lat)))
        tgt_cog = (bearing_deg + 180.0) % 360.0
        tgt_sog = 10.0 + self._rng.uniform(-2.0, 2.0) * adversarial_intensity

        scenario_id = f"rl_fuzzer_{rule}_{odd}_s{seed}"
        return {
            "title": f"RL Fuzzer {scenario_id}",
            "startTime": "2026-01-01T00:00:00Z",
            "ownShip": {
                "static": {"id": 1, "shipType": "Cargo", "name": "FCB Own Ship", "mmsi": 123456789},
                "initial": {
                    "position": {"latitude": own_lat, "longitude": own_lon},
                    "cog": 0.0, "sog": 10.0, "heading": 0.0,
                    "navStatus": "Under way using engine",
                },
                "model": "fcb_mmg_vessel", "controller": "psbmpc_wrapper",
            },
            "targetShips": [{
                "id": "ts1",
                "static": {"id": 2, "mmsi": 100000001},
                "initial": {
                    "position": {"latitude": tgt_lat, "longitude": tgt_lon},
                    "cog": tgt_cog, "sog": tgt_sog, "heading": tgt_cog,
                },
                "model": "ais_replay_vessel",
            }],
            "environment": {
                "wind": {"dir_deg": 270.0, "speed_mps": 5.0},
                "current": {"dir_deg": 0.0, "speed_mps": 0.0},
                "visibility_nm": 10.0,
            },
            "metadata": {
                "schema_version": "3.0",
                "scenario_id": scenario_id,
                "vessel_class": "FCB",
                "odd_cell": {"domain": odd},
                "encounter": {"rule": rule, "give_way_vessel": "own",
                              "expected_own_action": "turn_starboard"},
                "scenario_source": "rl_fuzzer_stub_d3.6",
                "adversarial_intensity": adversarial_intensity,
                "expected_outcome": {"cpa_min_m_ge": 500.0},
                "simulation_settings": {
                    "total_time": 1000.0, "dt": 0.02, "n_rps_initial": 3.0,
                    "coordinate_origin": [own_lat, own_lon],
                    "dynamics_mode": "internal", "backend": "ros2",
                },
            },
        }

    def load_failure_cases(self, failures_csv: str) -> None:
        """Seed perturbation bias from previous failure cases.

        D4.6 hook: RL agent trains on these trajectories.
        D3.6 impl: store for logging only; does not alter generate() behavior.
        """
        p = Path(failures_csv)
        if not p.exists():
            return
        with p.open() as f:
            self._failure_bias = list(csv.DictReader(f))
        print(f"[RLFuzzerStub] {len(self._failure_bias)} failure cases loaded (D4.6 training hook)")
