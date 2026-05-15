"""SIL Scoring Routes — read scoring.arrow → derived 8 KPI → REST response.

Design baseline: docs/Design/SIL/v1.0-unified/04-sil-scenario-integration-test.md §7
"""

import json
import sys
from pathlib import Path

from fastapi import APIRouter

from sil_orchestrator.config import RUN_DIR

router = APIRouter(prefix="/api/v1/scoring", tags=["scoring"])

_SCORING_PATH = Path(__file__).parent.parent / "sim_workbench" / "sil_nodes" / "scoring"
if str(_SCORING_PATH) not in sys.path:
    sys.path.insert(0, str(_SCORING_PATH))


def _get_latest_run_dir() -> Path | None:
    """Find the most recent run directory under RUN_DIR."""
    if not RUN_DIR.exists():
        return None
    dirs = sorted(
        [d for d in RUN_DIR.iterdir() if d.is_dir() and d.name.startswith("run-")],
        key=lambda d: d.stat().st_mtime,
        reverse=True,
    )
    return dirs[0] if dirs else None


@router.get("/last_run")
async def scoring_last_run():
    """Return 8 KPIs + 6-dim scores + rule_chain for the most recent run.

    Primary path: reads scoring.arrow via KpiDeriver → compute 8 KPIs + 6 dims.
    Fallback path: reads legacy scoring.json (DEMO-1 stub).
    """
    run_dir = _get_latest_run_dir()
    if run_dir is None:
        return {
            "run_id": None,
            "kpis": None,
            "rule_chain": [],
            "scoring_dimensions": None,
            "verdict": None,
        }

    run_id = run_dir.name
    arrow_path = run_dir / "scoring.arrow"

    # Primary path: scoring.arrow from scoring_node (ROS2 backend)
    if arrow_path.exists():
        try:
            import polars as pl
            from scoring.kpi_deriver import KpiDeriver

            deriver = KpiDeriver()
            kpis = deriver.derive_from_arrow(str(arrow_path))

            # Compute 6-dim aggregate scores from Arrow columns
            df = pl.read_ipc(str(arrow_path))
            dims = {
                "safety": round(df["safety"].mean(), 4),
                "rule_compliance": round(df["rule_compliance"].mean(), 4),
                "delay_penalty": round(df["delay_penalty"].mean(), 4),
                "action_magnitude_penalty": round(df["action_magnitude_penalty"].mean(), 4),
                "phase_score": round(df["phase_score"].mean(), 4),
                "plausibility": round(df["plausibility"].mean(), 4),
            }
            avg_total = float(df["total"].mean())
            dims["total"] = round(avg_total, 4)
            verdict = "pass" if avg_total >= 0.70 else "fail"

            # Read scenario_id from scenario.yaml if available
            scenario_id = None
            scenario_yaml_path = run_dir / "scenario.yaml"
            if scenario_yaml_path.exists():
                import yaml

                try:
                    yaml_data = yaml.safe_load(scenario_yaml_path.read_text())
                    if isinstance(yaml_data, dict):
                        meta = yaml_data.get("metadata", {})
                        if isinstance(meta, dict):
                            scenario_id = meta.get("scenario_id")
                except Exception:
                    pass

            return {
                "run_id": run_id,
                "scenario_id": scenario_id,
                "kpis": kpis,
                "scoring_dimensions": dims,
                "rule_chain": [],  # populated by M6 in Phase 2
                "verdict": verdict,
            }
        except Exception as e:
            return {
                "run_id": run_id,
                "kpis": None,
                "rule_chain": [],
                "scoring_dimensions": None,
                "verdict": None,
                "error": f"Arrow read failed: {e}",
            }

    # Fallback path: legacy scoring.json (DEMO-1)
    json_path = run_dir / "scoring.json"
    if json_path.exists():
        try:
            data = json.loads(json_path.read_text())
            data["run_id"] = run_id
            data.setdefault("scoring_dimensions", None)
            return data
        except Exception:
            pass

    return {
        "run_id": run_id,
        "kpis": None,
        "rule_chain": [],
        "scoring_dimensions": None,
        "verdict": None,
    }
