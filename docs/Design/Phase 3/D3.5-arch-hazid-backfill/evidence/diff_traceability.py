#!/usr/bin/env python3
"""D3.5 traceability diff tool.

Compares baseline traceability.csv (from D1.3.1/D3.6 CI) against a calibrated
regression run output. Writes a Markdown report and exits 1 if any FAIL/MISSING.

Expected CSV columns: scenario_id, rule, result (PASS/FAIL), cpa_min_m, tcpa_s,
                      colreg_compliance, [additional metrics as available]

Usage:
  python3 diff_traceability.py --baseline B.csv --calibrated C.csv --output report.md
"""
import argparse
import csv
import sys
from pathlib import Path


def load_csv(path: str) -> dict[str, dict]:
    rows = {}
    with open(path, newline='', encoding='utf-8') as f:
        for row in csv.DictReader(f):
            sid = row.get('scenario_id', '').strip()
            if sid:
                rows[sid] = row
    return rows


def format_delta(bv: str, cv: str) -> str:
    try:
        delta = float(cv) - float(bv)
        pct = (delta / float(bv) * 100) if float(bv) != 0 else float('inf')
        return f"{bv}->{cv} (D{delta:+.3f}, {pct:+.1f}%)"
    except (ValueError, TypeError):
        return f"{bv}->{cv}"


def main() -> None:
    p = argparse.ArgumentParser(description='D3.5 regression traceability diff')
    p.add_argument('--baseline', required=True, help='Baseline traceability CSV')
    p.add_argument('--calibrated', required=True, help='Calibrated run CSV')
    p.add_argument('--output', required=True, help='Output Markdown report path')
    args = p.parse_args()

    baseline = load_csv(args.baseline)
    calibrated = load_csv(args.calibrated)
    all_ids = sorted(set(baseline.keys()) | set(calibrated.keys()))

    failures: list[str] = []
    report_rows: list[dict] = []
    numeric_cols = ('cpa_min_m', 'tcpa_s')

    for sid in all_ids:
        b = baseline.get(sid)
        c = calibrated.get(sid)

        if c is None:
            failures.append(sid)
            report_rows.append({
                'scenario_id': sid, 'status': 'MISSING',
                'baseline': b.get('result', '?') if b else '?',
                'calibrated': 'N/A', 'diffs': 'scenario absent in calibrated run',
            })
            continue

        if b is None:
            report_rows.append({
                'scenario_id': sid, 'status': 'NEW',
                'baseline': 'N/A', 'calibrated': c.get('result', '?'), 'diffs': '-',
            })
            continue

        cal_result = c.get('result', 'UNKNOWN')
        status = 'PASS' if cal_result == 'PASS' else 'FAIL'
        if status == 'FAIL':
            failures.append(sid)

        diffs = []
        for col in numeric_cols:
            bv, cv_val = b.get(col, ''), c.get(col, '')
            if bv and cv_val and bv != cv_val:
                diffs.append(f"{col}: {format_delta(bv, cv_val)}")

        report_rows.append({
            'scenario_id': sid, 'status': status,
            'baseline': b.get('result', '?'), 'calibrated': cal_result,
            'diffs': '; '.join(diffs) or '-',
        })

    total = len(all_ids)
    n_pass = total - len(failures)
    out = Path(args.output)
    with open(out, 'w', encoding='utf-8') as f:
        f.write('# D3.5 HAZID Calibration Regression Report\n\n')
        f.write(f'| Field | Value |\n|---|---|\n')
        f.write(f'| Baseline | `{args.baseline}` |\n')
        f.write(f'| Calibrated | `{args.calibrated}` |\n')
        f.write(f'| Total scenarios | {total} |\n')
        f.write(f'| PASS | {n_pass} |\n')
        f.write(f'| FAIL/MISSING | {len(failures)} |\n\n')

        f.write('## Scenario Results\n\n')
        f.write('| scenario_id | status | baseline | calibrated | metric diffs |\n')
        f.write('|---|---|---|---|---|\n')
        for row in report_rows:
            f.write(
                f"| {row['scenario_id']} | **{row['status']}** | "
                f"{row['baseline']} | {row['calibrated']} | {row['diffs']} |\n"
            )

        if failures:
            f.write(f'\n## Failed Scenarios\n\n')
            for sid in failures:
                f.write(f'- `{sid}`\n')

    print(f'Report: {out}')
    if failures:
        print(f'FAIL: {len(failures)}/{total} scenarios: {failures}')
        sys.exit(1)
    print(f'PASS: all {total} scenarios passed')


if __name__ == '__main__':
    main()
