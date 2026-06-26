from pathlib import Path

from tools.sil.colregs_probe_matrix import compare_batches, summarize_batch


def write_batch(path: Path, rows: dict) -> None:
    import json

    path.write_text(json.dumps(rows), encoding="utf-8")


def test_summarize_batch_counts_passes_and_gate_families(tmp_path):
    batch = tmp_path / "batch.json"
    write_batch(batch, {
        "colreg-rule14-ho": {
            "overall_pass": False,
            "cpa_ok": False,
            "stability_pass": False,
            "returned_to_route": False,
            "route_return_required": True,
            "route_corridor_ok": True,
            "domain_gates": {"risk_gate_ok": True, "seamanship_gate_ok": True},
            "phase_semantics": {"phase_semantics_ok": True},
        },
        "colreg-rule13-ot-target-giveway": {
            "overall_pass": True,
            "cpa_ok": True,
            "stability_pass": True,
            "returned_to_route": True,
            "route_return_required": True,
            "route_corridor_ok": True,
            "domain_gates": {"risk_gate_ok": True, "seamanship_gate_ok": True},
            "phase_semantics": {"phase_semantics_ok": True},
        },
    })

    matrix = summarize_batch(batch)

    assert matrix.pass_count == 1
    assert matrix.total_count == 2
    assert matrix.rows["colreg-rule14-ho"].families == {
        "CPA", "Stability", "RouteReturn"
    }


def test_compare_batches_flags_previously_green_regression(tmp_path):
    baseline_path = tmp_path / "base.json"
    current_path = tmp_path / "cur.json"
    write_batch(baseline_path, {
        "colreg-rule14-ho": {"overall_pass": True, "cpa_ok": True,
                             "stability_pass": True, "returned_to_route": True,
                             "route_return_required": True,
                             "route_corridor_ok": True,
                             "domain_gates": {"risk_gate_ok": True, "seamanship_gate_ok": True},
                             "phase_semantics": {"phase_semantics_ok": True}},
    })
    write_batch(current_path, {
        "colreg-rule14-ho": {"overall_pass": False, "cpa_ok": False,
                             "stability_pass": True, "returned_to_route": False,
                             "route_return_required": True,
                             "route_corridor_ok": True,
                             "domain_gates": {"risk_gate_ok": True, "seamanship_gate_ok": True},
                             "phase_semantics": {"phase_semantics_ok": True}},
    })

    report = compare_batches(summarize_batch(baseline_path), summarize_batch(current_path))

    assert report.regressed_scenarios == ["colreg-rule14-ho"]
    assert report.current_pass_count == 0
    assert report.baseline_pass_count == 1
