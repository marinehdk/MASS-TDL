from pathlib import Path
import argparse
import json
import sys

import yaml


def _load_mapping(path):
    with open(path, 'r', encoding='utf-8') as stream:
        if Path(path).suffix.lower() == '.json':
            return json.load(stream) or {}
        return yaml.safe_load(stream) or {}


def _default_gates_path():
    return Path(__file__).resolve().parents[1] / 'config' / 'validation' / 'regression_gates.yaml'


def _observed_metrics(path, scenario_id):
    raw = _load_mapping(path)
    scenarios = raw.get('scenarios', raw)
    return scenarios.get(scenario_id, {}) or {}


def _gate_status(key, expected, observed):
    if key not in observed:
        return 'FAIL', 'missing observed metric'

    value = observed[key]
    if isinstance(expected, bool):
        passed = bool(value) == expected
        return ('PASS' if passed else 'FAIL'), f'{value} == {expected}'

    if key.startswith('max_') and isinstance(expected, (int, float)):
        passed = float(value) <= float(expected)
        return ('PASS' if passed else 'FAIL'), f'{value} <= {expected}'

    if key.startswith('min_') and isinstance(expected, (int, float)):
        passed = float(value) >= float(expected)
        return ('PASS' if passed else 'FAIL'), f'{value} >= {expected}'

    passed = value == expected
    return ('PASS' if passed else 'FAIL'), f'{value} == {expected}'


def check_gates(metrics_path, gates_path, baseline):
    gates = _load_mapping(gates_path)
    baseline_cfg = (gates.get('golden_baselines', {}) or {}).get(baseline, {})
    gate_values = baseline_cfg.get('gates', {}) or {}
    if not gate_values:
        raise ValueError(f'No golden baseline gates found for {baseline}')

    observed = _observed_metrics(metrics_path, baseline)
    rows = []
    for key, expected in gate_values.items():
        status, detail = _gate_status(key, expected, observed)
        rows.append({
            'baseline': baseline,
            'metric': key,
            'expected': expected,
            'observed': observed.get(key, ''),
            'status': status,
            'detail': detail,
        })
    return rows


def _print_rows(rows):
    print(f"\nGolden baseline: {rows[0]['baseline'] if rows else '-'}")
    for row in rows:
        observed = row['observed'] if row['observed'] != '' else '-'
        print(
            f"  {row['status']:<5} {row['metric']}: "
            f"expected={row['expected']} observed={observed} {row['detail']}"
        )


def main(argv=None):
    parser = argparse.ArgumentParser(description='Check observed metrics against regression gates.')
    parser.add_argument('--metrics', required=True, help='YAML/JSON metrics report.')
    parser.add_argument('--gates', default=str(_default_gates_path()), help='Regression gates YAML.')
    parser.add_argument('--baseline', default='001_straight_calm', help='Golden baseline id.')
    parser.add_argument('--json-out', help='Write machine-readable report.')
    args = parser.parse_args(argv)

    rows = check_gates(Path(args.metrics), Path(args.gates), args.baseline)
    _print_rows(rows)
    if args.json_out:
        with open(args.json_out, 'w', encoding='utf-8') as stream:
            json.dump(rows, stream, indent=2, ensure_ascii=False)

    failed = [row for row in rows if row['status'] == 'FAIL']
    print(
        f"\nSUMMARY: {len(failed)} failed, "
        f"{sum(1 for row in rows if row['status'] == 'PASS')} passed"
    )
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main(sys.argv[1:])
