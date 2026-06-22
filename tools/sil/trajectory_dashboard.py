from __future__ import annotations

import bisect
import json
import math
import statistics
from collections import Counter
from pathlib import Path
from typing import Any

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import LineCollection
from matplotlib.lines import Line2D
from matplotlib.patches import FancyBboxPatch, Rectangle

R_EARTH_M = 6371008.8

DASHBOARD_STATIC_LABELS = (
    "Own-Ship Track",
    "Phase Legend",
    "Navigation",
    "Avoidance",
    "Recovery",
    "Trace Summary",
)


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open() as f:
        for line in f:
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return records


def _fval(record: dict[str, Any], key: str, default: float = 0.0) -> float:
    try:
        value = record.get(key, default)
        return default if value is None else float(value)
    except (TypeError, ValueError):
        return default


def _geo_delta(ref: dict[str, Any], record: dict[str, Any]) -> tuple[float, float]:
    c = math.cos(math.radians(_fval(ref, "lat")))
    east = math.radians(_fval(record, "lon") - _fval(ref, "lon")) * R_EARTH_M * c
    north = math.radians(_fval(record, "lat") - _fval(ref, "lat")) * R_EARTH_M
    return east, north


def _geo_dist(a: dict[str, Any], b: dict[str, Any]) -> float:
    east, north = _geo_delta(a, b)
    return math.hypot(east, north)


def _dominant_ownship_segment(ownship: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if not ownship:
        return []
    segments: list[list[dict[str, Any]]] = []
    current = [ownship[0]]
    for prev, cur in zip(ownship, ownship[1:]):
        if _geo_dist(prev, cur) > 1000.0:
            segments.append(current)
            current = [cur]
        else:
            current.append(cur)
    segments.append(current)
    return max(segments, key=len)


def _behavior_class(record: dict[str, Any]) -> str:
    behavior = int(record.get("behavior") or 0)
    active = bool(record.get("avoidance_active"))
    if behavior == 7:
        return "RECOVERY"
    if active or behavior in (1, 2):
        return "AVOIDANCE"
    return "TRANSIT"


def _status_color(ok: Any) -> str:
    if ok is True:
        return "#16a34a"
    if ok is False:
        return "#dc2626"
    return "#64748b"


def _status_text(ok: Any) -> str:
    if ok is True:
        return "PASS"
    if ok is False:
        return "FAIL"
    return "UNKNOWN"


def _shorten(text: str, limit: int = 56) -> str:
    if len(text) <= limit:
        return text
    return text[: limit - 3] + "..."


def generate_trajectory_dashboard(
    *,
    trace_jsonl: Path,
    output_png: Path,
    scenario_id: str,
    session_name: str,
    report_json: Path | None = None,
) -> Path:
    records = _read_jsonl(Path(trace_jsonl))
    by_topic: dict[str, list[dict[str, Any]]] = {}
    for record in records:
        by_topic.setdefault(str(record.get("topic")), []).append(record)

    own = _dominant_ownship_segment(by_topic.get("/sil/own_ship_state", []))
    if len(own) < 2:
        raise ValueError(f"Not enough own-ship samples in {trace_jsonl}")

    report: dict[str, Any] = {}
    if report_json and Path(report_json).exists():
        report = json.loads(Path(report_json).read_text())

    ref = own[0]
    xs: list[float] = []
    ys: list[float] = []
    ts: list[float] = []
    hdgs: list[float] = []
    sogs: list[float] = []
    for row in own:
        east, north = _geo_delta(ref, row)
        xs.append(east)
        ys.append(north)
        ts.append(_fval(row, "sim_t"))
        hdgs.append(_fval(row, "heading_deg"))
        sogs.append(_fval(row, "sog_kn"))

    path_m = sum(_geo_dist(a, b) for a, b in zip(own, own[1:]))
    t0, t1 = ts[0], ts[-1]

    m4_all = by_topic.get("/l3/m4/behavior_plan", [])
    m4_times = [_fval(row, "sim_t") for row in m4_all]

    def class_at(t: float) -> str:
        idx = bisect.bisect_right(m4_times, t) - 1
        if idx < 0:
            return "TRANSIT"
        return _behavior_class(m4_all[idx])

    classes = [class_at(t) for t in ts]
    colors = {
        "TRANSIT": "#2563eb",
        "AVOIDANCE": "#dc2626",
        "RECOVERY": "#7c3aed",
    }
    points = list(zip(xs, ys))
    line_segments = [[points[i], points[i + 1]] for i in range(len(points) - 1)]
    line_colors = [colors[classes[i]] for i in range(len(line_segments))]

    transitions: list[tuple[float, str]] = []
    last_class: str | None = None
    for row in m4_all:
        rt = _fval(row, "sim_t")
        if t0 <= rt <= t1:
            klass = _behavior_class(row)
            if klass != last_class:
                transitions.append((rt, klass))
                last_class = klass

    m6 = [
        r for r in by_topic.get("/l3/m6/colregs_constraint", [])
        if t0 <= _fval(r, "sim_t") <= t1
    ]
    m5 = [
        r for r in by_topic.get("/l3/m5/avoidance_plan", [])
        if t0 <= _fval(r, "sim_t") <= t1
    ]
    scoring = [
        r for r in by_topic.get("/sil/scoring", [])
        if t0 <= _fval(r, "sim_t") <= t1
    ]
    actuator = [
        r for r in by_topic.get("/sil/actuator_cmd", [])
        if t0 <= _fval(r, "sim_t") <= t1
    ]

    xte = [abs(x) for x in xs]
    max_idx = max(range(len(xs)), key=lambda idx: xte[idx])
    phase_counter = Counter(str(r.get("phase")) for r in m6)
    direction_counter = Counter(str(r.get("primary_preferred_direction")) for r in m6)
    valid_m5 = sum(1 for r in m5 if str(r.get("solver_status")) == "VALID")
    empty_m5 = sum(1 for r in m5 if str(r.get("solver_status")) == "EMPTY")
    score_totals = [_fval(r, "total") for r in scoring]
    rudders = [abs(_fval(r, "rudder_deg")) for r in actuator]
    chain = report.get("chain_summary") or {}
    chain_diag = chain.get("diagnosis") if isinstance(chain, dict) else {}
    if not isinstance(chain_diag, dict):
        chain_diag = {}
    chain_diag_text = (
        f"{chain_diag.get('first_broken_stage', 'UNKNOWN')}: "
        f"{chain_diag.get('reason', 'missing')}"
    )

    plt.rcParams.update({
        "font.family": "DejaVu Sans",
        "axes.unicode_minus": False,
        "axes.titlesize": 13,
        "axes.labelsize": 11,
        "xtick.labelsize": 9,
        "ytick.labelsize": 9,
    })
    fig = plt.figure(figsize=(15.4, 11.2), dpi=100, facecolor="#f8fafc")
    fig.suptitle(
        f"{DASHBOARD_STATIC_LABELS[0]}: {scenario_id}_{session_name}",
        fontsize=18,
        y=0.965,
        weight="bold",
        color="#0f172a",
    )
    legend_ax = fig.add_axes([0.035, 0.115, 0.125, 0.775])
    legend_ax.axis("off")
    ax = fig.add_axes([0.185, 0.095, 0.295, 0.81])
    panel = fig.add_axes([0.525, 0.085, 0.43, 0.82])
    panel.axis("off")

    legend_ax.text(
        0.0, 0.98, DASHBOARD_STATIC_LABELS[1],
        fontsize=12.5, weight="bold", color="#0f172a", va="top",
        transform=legend_ax.transAxes,
    )
    legend_y = 0.90
    for key, label in (
        ("TRANSIT", DASHBOARD_STATIC_LABELS[2]),
        ("AVOIDANCE", DASHBOARD_STATIC_LABELS[3]),
        ("RECOVERY", DASHBOARD_STATIC_LABELS[4]),
    ):
        legend_ax.add_line(Line2D(
            [0.02, 0.32], [legend_y, legend_y],
            transform=legend_ax.transAxes,
            color=colors[key],
            lw=4,
            solid_capstyle="round",
        ))
        legend_ax.text(
            0.38, legend_y + 0.018, key,
            fontsize=9.5, weight="bold", color="#111827", va="center",
            transform=legend_ax.transAxes,
        )
        legend_ax.text(
            0.38, legend_y - 0.020, label,
            fontsize=8.5, color="#64748b", va="center",
            transform=legend_ax.transAxes,
        )
        legend_y -= 0.105

    for marker_y, color, label, marker in (
        (legend_y, "#22c55e", "OS Start", "o"),
        (legend_y - 0.065, "#2563eb", "OS End", "o"),
        (legend_y - 0.13, "#f97316", "Max XTE", "D"),
    ):
        legend_ax.scatter(
            [0.08], [marker_y], s=80, color=color, edgecolor="black",
            lw=.7, marker=marker, transform=legend_ax.transAxes, zorder=3,
        )
        legend_ax.text(
            0.20, marker_y, label,
            fontsize=9.5, weight="bold", color="#111827", va="center",
            transform=legend_ax.transAxes,
        )

    metric_y = 0.35
    legend_ax.text(
        0.0, metric_y, DASHBOARD_STATIC_LABELS[5],
        fontsize=12.0, weight="bold", color="#0f172a", va="top",
        transform=legend_ax.transAxes,
    )
    for idx, (key, value) in enumerate((
        ("points", f"{len(own):,}"),
        ("duration", f"{t1 - t0:.1f}s"),
        ("path", f"{path_m:.1f}m"),
        ("max XTE", f"{max(xte):.1f}m"),
    )):
        yy = metric_y - 0.055 - idx * 0.045
        legend_ax.text(
            0.0, yy, key,
            fontsize=8.5, color="#64748b", va="top",
            transform=legend_ax.transAxes,
        )
        legend_ax.text(
            0.62, yy, value,
            fontsize=8.7, weight="bold", color="#111827", va="top", ha="right",
            transform=legend_ax.transAxes,
        )

    ax.set_facecolor("white")
    for spine in ax.spines.values():
        spine.set_color("#334155")
        spine.set_linewidth(1)
    ax.grid(True, color="#cbd5e1", lw=.8)
    ax.add_collection(LineCollection(
        line_segments,
        colors=line_colors,
        linewidths=2.8,
        capstyle="round",
        joinstyle="round",
        zorder=4,
    ))
    ax.scatter([xs[0]], [ys[0]], s=88, color="#22c55e", edgecolor="black", lw=.8, zorder=8)
    ax.scatter([xs[-1]], [ys[-1]], s=88, color="#2563eb", edgecolor="black", lw=.8, zorder=8)
    ax.scatter([xs[max_idx]], [ys[max_idx]], s=70, marker="D", color="#f97316", edgecolor="black", lw=.6, zorder=8)
    ax.annotate(
        f"Max XTE {xte[max_idx]:.1f}m",
        xy=(xs[max_idx], ys[max_idx]),
        xytext=(15, -24),
        textcoords="offset points",
        fontsize=8.8,
        color="#9a3412",
        arrowprops=dict(arrowstyle="->", color="#f97316", lw=1),
    )
    ax.set_xlim(min(min(xs), -55) - 35, max(max(xs), 55) + 110)
    ax.set_ylim(min(-75, min(ys) - 35), max(max(ys) + 130, 2550))
    ax.set_aspect("equal", adjustable="box")
    ax.set_xlabel("East from start (m)")
    ax.set_ylabel("North from start (m)")
    ax.set_title("Full Trace", pad=10)

    def panel_text(
        x: float,
        y0: float,
        text: str,
        size: float = 9,
        color: str = "#334155",
        weight: str | None = None,
        ha: str = "left",
    ) -> None:
        panel.text(
            x, y0, text, transform=panel.transAxes, fontsize=size,
            color=color, weight=weight, va="top", ha=ha,
        )

    def card(top: float, height: float, title: str) -> float:
        panel.add_patch(FancyBboxPatch(
            (0.02, top - height), 0.96, height,
            boxstyle="round,pad=0.010,rounding_size=0.012",
            lw=.9,
            edgecolor="#cbd5e1",
            facecolor="white",
            transform=panel.transAxes,
        ))
        panel.text(
            0.05, top - 0.032, title,
            transform=panel.transAxes,
            fontsize=12.5,
            weight="bold",
            color="#0f172a",
            va="top",
        )
        return top - 0.068

    verdict = report.get("verdict", {})
    overall = verdict.get("overall_pass")
    panel.add_patch(Rectangle(
        (0.02, 0.925), 0.96, 0.055,
        transform=panel.transAxes,
        facecolor=_status_color(overall),
        edgecolor="none",
    ))
    panel.text(
        0.05, 0.952, f"OVERALL {_status_text(overall)}",
        transform=panel.transAxes,
        fontsize=15.5,
        weight="bold",
        color="white",
        va="center",
    )
    panel.text(
        0.95, 0.952, scenario_id,
        transform=panel.transAxes,
        fontsize=9.5,
        color="white",
        va="center",
        ha="right",
    )

    core_y = card(0.895, 0.205, "Core Run Data")
    for idx, (key, value) in enumerate((
        ("Trace points", f"{len(own):,}"),
        ("Sim time", f"{t0:.1f}s -> {t1:.1f}s ({t1 - t0:.1f}s)"),
        ("Path length", f"{path_m:.1f} m"),
        ("Max XTE", f"{max(xte):.1f} m"),
        ("Start", f"SOG {sogs[0]:.1f} kn | HDG {hdgs[0]:.1f} deg"),
        ("End", f"SOG {sogs[-1]:.1f} kn | HDG {hdgs[-1]:.1f} deg"),
    )):
        yy = core_y - idx * .022
        panel_text(.05, yy, key, 8.5, "#64748b")
        panel_text(.36, yy, value, 8.8, "#111827", "bold")

    gate_y = card(0.66, 0.300, "GATE + Layer Checks")
    for idx, (name, ok) in enumerate((
        ("Safety", verdict.get("safety_pass")),
        ("Mission", verdict.get("mission_pass")),
        ("COLREGs", verdict.get("colregs_pass")),
        ("Stability", verdict.get("stability_pass")),
    )):
        x0 = .05 + (idx % 2) * .47
        yy = gate_y - (idx // 2) * .047
        panel.add_patch(Rectangle(
            (x0, yy - .026), .14, .032,
            transform=panel.transAxes,
            facecolor=_status_color(ok),
            edgecolor="none",
        ))
        panel.text(
            x0 + .07, yy - .010, _status_text(ok),
            transform=panel.transAxes,
            fontsize=8.2,
            color="white",
            weight="bold",
            ha="center",
            va="center",
        )
        panel_text(x0 + .16, yy, name, 8.9, "#111827", "bold")
    provenance = report.get("threshold_provenance", {})
    panel_text(.05, gate_y - .103, "CPA floor", 8.4, "#64748b")
    panel_text(
        .27, gate_y - .103,
        f"{provenance.get('threshold_m', 'UNKNOWN')} m ({provenance.get('threshold_formula', 'UNKNOWN')})",
        8.7, "#111827", "bold",
    )
    panel_text(.55, gate_y - .103, "First failure", 8.4, "#64748b")
    panel_text(.78, gate_y - .103, str(report.get("first_failure") or "None"), 8.7, "#111827", "bold")

    layers = list(report.get("layers", {}).items())
    for idx, (layer_name, data) in enumerate(layers[:7]):
        col = 0 if idx < 4 else 1
        row = idx if idx < 4 else idx - 4
        x0 = .05 if col == 0 else .55
        stat_x = .44 if col == 0 else .94
        yy = gate_y - .148 - row * .024
        status = data.get("status") if isinstance(data, dict) else data
        color = "#16a34a" if status == "PASS" else ("#dc2626" if status == "FAIL" else "#64748b")
        panel_text(x0, yy, layer_name.replace("_", " ")[:21], 7.8, "#334155")
        panel_text(stat_x, yy, str(status), 7.9, color, "bold", "right")

    trace_y = card(0.335, 0.245, "Trace Signals")
    trans = " -> ".join(
        f"{klass.replace('AVOIDANCE', 'AVOID')}@{t:.0f}s"
        for t, klass in transitions
    )
    signals = [
        ("M4 transitions", _shorten(trans or "None")),
        ("M6 conflict", f"{sum(1 for r in m6 if r.get('conflict_detected'))}/{len(m6)} samples"),
        ("M6 phase", _shorten(", ".join(f"{k}:{v}" for k, v in phase_counter.most_common(2)) or "UNKNOWN")),
        ("M6 direction", _shorten(", ".join(f"{k}:{v}" for k, v in direction_counter.most_common(2)) or "UNKNOWN")),
        ("M5 solver", f"VALID {valid_m5} | EMPTY {empty_m5}"),
        ("Scoring total", f"avg {statistics.mean(score_totals):.3f} | min {min(score_totals):.3f}" if score_totals else "UNKNOWN"),
        ("Max rudder", f"{max(rudders):.1f} deg" if rudders else "UNKNOWN"),
        ("Chain diagnosis", _shorten(chain_diag_text)),
    ]
    for idx, (key, value) in enumerate(signals):
        yy = trace_y - idx * .024
        panel_text(.05, yy, key, 8.3, "#64748b")
        panel_text(.33, yy, value, 8.3, "#111827", "bold")

    panel.add_patch(FancyBboxPatch(
        (0.02, 0.010), 0.96, 0.075,
        boxstyle="round,pad=0.010,rounding_size=0.012",
        lw=.9,
        edgecolor="#cbd5e1",
        facecolor="white",
        transform=panel.transAxes,
    ))
    panel_text(.05, .065, "Displayed checks", 8.5, "#64748b")
    panel_text(
        .30, .065,
        "Safety / Mission / COLREGs / Stability / L1-L7 / M4-M6 / M5 solver / Chain / Rudder",
        8.1,
        "#111827",
        "bold",
    )

    output_png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_png, facecolor=fig.get_facecolor())
    plt.close(fig)
    return output_png
