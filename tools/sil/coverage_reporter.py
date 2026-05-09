"""D1.3b COLREGs coverage HTML report generator."""
from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path

from jinja2 import Environment, FileSystemLoader

TEMPLATES_DIR = Path(__file__).parent / "templates"

RULE_ORDER = [
    "Rule 14 Head-on",
    "Rule 15/16 Stbd",
    "Rule 15/17 Port",
    "Rule 13 Overtaking",
]


def generate_report(results_dir: Path, output_path: Path) -> None:
    """Load all JSON results from results_dir, render HTML report to output_path."""
    json_files = sorted(results_dir.glob("*.json"))
    json_files = [f for f in json_files if f.name != "batch_results.json"]

    results = []
    for jf in json_files:
        try:
            data = json.loads(jf.read_text())
            data["_json_filename"] = jf.name
            results.append(data)
        except (json.JSONDecodeError, KeyError):
            continue

    # Build matrix rows
    matrix = []
    for r in results:
        sc = r.get("sub_checks", {})
        rule_label = _infer_rule_label(r.get("scenario_id", ""))
        matrix.append({
            "rule": rule_label,
            "scenario_id": r.get("scenario_id", "?"),
            "geometric": sc.get("geometric_compliance", False),
            "solvability": sc.get("solvability", False),
            "stability": sc.get("stability", False),
            "wall_clock": sc.get("wall_clock_le_60s", False),
            "overall": r.get("result") == "PASS",
            "json_path": r["_json_filename"],
        })

    # Sort matrix by rule order
    matrix.sort(key=lambda x: (RULE_ORDER.index(x["rule"]) if x["rule"] in RULE_ORDER else 99, x["scenario_id"]))

    # Build details
    details = []
    for r in results:
        m = r.get("metrics", {})
        dist = r.get("disturbance_recorded", {})
        details.append({
            "scenario_id": r.get("scenario_id", "?"),
            "overall": r.get("result") == "PASS",
            "dcpa_no_action_m": m.get("dcpa_no_action_m", 0.0),
            "dcpa_with_action_m": m.get("dcpa_with_action_m", 0.0),
            "tcpa_no_action_s": m.get("tcpa_no_action_s", 0.0),
            "wall_clock_s": r.get("performance", {}).get("wall_clock_s", 0.0),
            "wind_kn": dist.get("wind_kn", 0.0),
            "current_kn": dist.get("current_kn", 0.0),
            "vis_m": dist.get("vis_m", 0.0),
        })

    env = Environment(loader=FileSystemLoader(str(TEMPLATES_DIR)), autoescape=False)
    tpl = env.get_template("coverage_report.html.j2")
    html = tpl.render(
        timestamp=datetime.now(tz=timezone.utc).isoformat(),
        total=len(results),
        passed=sum(1 for r in results if r.get("result") == "PASS"),
        failed=sum(1 for r in results if r.get("result") == "FAIL"),
        matrix=matrix,
        details=details,
    )
    output_path.write_text(html, encoding="utf-8")


def _infer_rule_label(scenario_id: str) -> str:
    if "ho-" in scenario_id:
        return "Rule 14 Head-on"
    if "cs-001" in scenario_id or "cs-002" in scenario_id or "cs-004" in scenario_id:
        return "Rule 15/16 Stbd"
    if "cs-003" in scenario_id:
        return "Rule 15/17 Port"
    if "ot-" in scenario_id:
        return "Rule 13 Overtaking"
    return "Unknown"
