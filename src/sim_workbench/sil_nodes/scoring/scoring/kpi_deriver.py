from pathlib import Path
from typing import Dict, Optional

import polars as pl


class KpiDeriver:
    """从 scoring.arrow 和可选 own_ship / ASDR 文件中派生 8 项 KPI。"""

    def derive_from_arrow(
        self,
        scoring_arrow_path: str,
        own_ship_arrow_path: Optional[str] = None,
        asdr_events_path: Optional[str] = None,
        grounding_risk: Optional[float] = None,
    ) -> Dict[str, float]:
        df = pl.read_ipc(scoring_arrow_path)

        if len(df) == 0:
            return {
                "min_cpa_nm": 0.0,
                "tcpa_min_s": 0.0,
                "avg_rot_dpm": 0.0,
                "max_rudder_deg": 0.0,
                "grounding_risk_score": 1.0,
                "route_deviation_nm": 0.0,
                "time_to_mrm_s": 0.0,
                "decision_count": 0,
            }

        # --- scoring 派生的 KPI ---
        min_cpa_nm = float(df["cpa_nm"].min())  # type: ignore[arg-type]
        min_idx_raw = df["cpa_nm"].arg_min()
        min_idx = min_idx_raw if min_idx_raw is not None else 0
        tcpa_min_s = float(df["stamp"][min_idx])
        decision_count = len(df)

        # --- own_ship 派生的 KPI ---
        avg_rot_dpm: float = 0.0
        max_rudder_deg: float = 0.0
        route_deviation_nm: float = 0.0
        if own_ship_arrow_path and Path(own_ship_arrow_path).exists():
            own_df = pl.read_ipc(own_ship_arrow_path)
            if "rot_dps" in own_df.columns:
                rot_mean: float = own_df["rot_dps"].abs().mean()  # type: ignore[arg-type]
                avg_rot_dpm = rot_mean * 60.0
            if "rudder_angle_deg" in own_df.columns:
                max_rudder_deg = own_df["rudder_angle_deg"].abs().max()  # type: ignore[assignment]
            if "cross_track_error_nm" in own_df.columns:
                route_deviation_nm = own_df["cross_track_error_nm"].abs().max()  # type: ignore[assignment]

        # --- grounding risk ---
        grounding_risk_score: float = grounding_risk if grounding_risk is not None else 1.0

        # --- time_to_mrm (stub) ---
        time_to_mrm_s = 0.0

        return {
            "min_cpa_nm": round(min_cpa_nm, 4),
            "tcpa_min_s": round(tcpa_min_s, 1),
            "avg_rot_dpm": round(avg_rot_dpm, 2),
            "max_rudder_deg": round(max_rudder_deg, 1),
            "grounding_risk_score": round(grounding_risk_score, 4),
            "route_deviation_nm": round(route_deviation_nm, 4),
            "time_to_mrm_s": round(time_to_mrm_s, 1),
            "decision_count": int(decision_count),
        }
