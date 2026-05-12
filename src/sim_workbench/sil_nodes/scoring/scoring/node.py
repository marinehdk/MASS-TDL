"""Scoring Node — 6-dim scoring (Hagen 2022): safety, rule, delay, magnitude, phase, plausibility."""


class ScoringNode:
    """Six-dimensional scenario scoring per Hagen (2022).

    Dimensions
    ----------
    safety          : CPA normalised against a 1 NM threshold.
    rule_compliance : binary — 1.0 if no rule broken, else 0.0.
    delay           : exponential decay from 60 s allowed lag.
    magnitude       : path deviation normalised against 500 m.
    phase           : reserved / structural (stub = 1.0).
    plausibility    : reserved / structural (stub = 1.0).
    """

    DIMS = ["safety", "rule_compliance", "delay", "magnitude", "phase", "plausibility"]

    def __init__(self, weights: dict | None = None) -> None:
        self._weights = weights or {d: 1.0 / 6 for d in self.DIMS}
        self._rows: list[dict] = []

    def score(
        self,
        timestamp: float,
        cpa: float = 1.0,
        rule_broken: bool = False,
        delay_s: float = 0.0,
        path_deviation: float = 0.0,
    ) -> dict:
        """Produce one scored row for a single simulation timestep."""
        safety = min(1.0, cpa / 1.0)
        rule = 0.0 if rule_broken else 1.0
        delay_score = max(0.0, 1.0 - delay_s / 60.0)
        magnitude = max(0.0, 1.0 - path_deviation / 500.0)
        phase = 1.0
        plausibility = 1.0

        total = (
            self._weights["safety"] * safety
            + self._weights["rule_compliance"] * rule
            + self._weights["delay"] * delay_score
            + self._weights["magnitude"] * magnitude
            + self._weights["phase"] * phase
            + self._weights["plausibility"] * plausibility
        )

        row = {
            "stamp": timestamp,
            "safety": safety,
            "rule_compliance": rule,
            "delay": delay_score,
            "magnitude": magnitude,
            "phase": phase,
            "plausibility": plausibility,
            "total": total,
        }
        self._rows.append(row)
        return row

    def get_rows(self) -> list[dict]:
        """Return all scored rows."""
        return list(self._rows)

    def get_final_verdict(self, threshold: float = 0.7) -> tuple[bool, float]:
        """Aggregate average total score across all rows.

        Returns (pass, average).
        """
        if not self._rows:
            return False, 0.0
        avg = sum(r["total"] for r in self._rows) / len(self._rows)
        return avg >= threshold, avg


def main() -> None:
    """CLI entry point (ROS2 console_scripts stub)."""
    print("ScoringNode ready")
