"""D1.3b / D1.3.2.1 COLREGs coverage HTML report generator (32-scenario matrix)."""
from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path

from jinja2 import Environment, FileSystemLoader

TEMPLATES_DIR = Path(__file__).parent / "templates"

RULE_ORDER = ["Rule13", "Rule14", "Rule15", "Multi-Ship"]

RULE_COVERAGE_ROWS = ["Rule13", "Rule14", "Rule15", "Multi-Ship"]
UNCOVERED_RULES = ["Rule5", "Rule6", "Rule7", "Rule8", "Rule9", "Rule17", "Rule19"]


def _infer_rule_label(scenario_id: str, metadata: dict | None = None) -> str:
    """Infer COLREGs rule label from metadata or scenario_id pattern.

    Priority 1: metadata.encounter.rule (with multi-ship override)
    Priority 2: ID pattern matching (ho/head → Rule14, cr-gw/cr-so/cs → Rule15,
                 ot/overtaking → Rule13, ms → Multi-Ship)
    Fallback: "Unknown"
    """
    # Priority 1: metadata.encounter.rule
    if metadata:
        enc = metadata.get("encounter", {})
        if isinstance(enc, dict):
            rule = enc.get("rule", "")
            if rule:
                n_targets = len(
                    metadata.get("initial_conditions", {}).get("targets", [])
                )
                if rule in ("Rule13", "Rule14", "Rule15") and n_targets > 1:
                    return "Multi-Ship"
                if rule in ("Rule13", "Rule14", "Rule15"):
                    return rule
                return rule

    # Priority 2: ID pattern matching
    sid = scenario_id.lower()

    # Multi-Ship (check first to avoid matching "-ho" inside something else)
    if "-ms" in sid or sid.startswith("ms-"):
        return "Multi-Ship"

    # Head-on → Rule14
    if "-ho" in sid or "head" in sid:
        return "Rule14"

    # Overtaking → Rule13
    if "-ot" in sid or "overtaking" in sid:
        return "Rule13"

    # Crossing → Rule15 (cr-gw, cr-so, cs- anywhere in ID)
    if "-cr-" in sid or sid.startswith("cr-") or "-cs" in sid or sid.startswith("cs-"):
        return "Rule15"

    return "Unknown"


def build_coverage_matrix(batch_results: list[dict]) -> list[dict]:
    """Build the Rule × Scenario coverage matrix from batch results.

    Returns a list of row dicts sorted by RULE_ORDER then scenario_id,
    with _section_start=True on the first row of each rule group
    (used by the Jinja2 template for section headers).
    """
    matrix = []
    for r in batch_results:
        sc = r.get("sub_checks", {})
        rule_label = _infer_rule_label(
            r.get("scenario_id", ""),
            metadata=r,
        )
        matrix.append({
            "rule": rule_label,
            "scenario_id": r.get("scenario_id", "?"),
            "geometric": sc.get("geometric_compliance", False),
            "solvability": sc.get("solvability", False),
            "stability": sc.get("stability", False),
            "wall_clock": sc.get("wall_clock_le_60s", False),
            "overall": r.get("result") == "PASS",
            "json_path": r.get("_json_filename", ""),
        })

    # Sort by rule order, then scenario_id
    matrix.sort(key=lambda x: (
        RULE_ORDER.index(x["rule"]) if x["rule"] in RULE_ORDER else 99,
        x["scenario_id"],
    ))

    # Insert section-start markers for template rendering
    last_rule: str | None = None
    for row in matrix:
        if row["rule"] != last_rule:
            row["_section_start"] = True
            last_rule = row["rule"]

    return matrix


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

    # Build rule × scenario matrix
    matrix = build_coverage_matrix(results)

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
        rule_coverage_rows=RULE_COVERAGE_ROWS,
        uncovered_rules=UNCOVERED_RULES,
    )
    output_path.write_text(html, encoding="utf-8")
