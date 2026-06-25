from pathlib import Path
import argparse
import json
import sys

import yaml

try:
    from ament_index_python.packages import PackageNotFoundError, get_package_share_directory
except ImportError:
    class PackageNotFoundError(Exception):
        pass

    def get_package_share_directory(_package_name):
        raise PackageNotFoundError()


TEXT_KEYS = {'pass_fail_basis'}


def _load_mapping(path):
    with open(path, 'r', encoding='utf-8') as stream:
        if path.suffix.lower() == '.json':
            return json.load(stream) or {}
        return yaml.safe_load(stream) or {}


def _default_scenarios_root():
    return Path(__file__).resolve().parents[1] / 'config' / 'scenarios'


def _resolve_scenarios(args):
    if args.scenario:
        return [Path(item) for item in args.scenario]
    root = Path(args.scenario_dir) if args.scenario_dir else _default_scenarios_root()
    return sorted(root.glob('*.yaml'))


def _metric_status(key, expected_value, metrics, metric_contract=None):
    if key in TEXT_KEYS:
        return 'INFO', 'basis', str(expected_value)

    if key not in metrics:
        if metric_contract:
            interface = metric_contract.get('interface', 'unknown interface')
            source = metric_contract.get('source', 'unknown source')
            observable_now = metric_contract.get('observable_now', False)
            return (
                'PENDING',
                f'missing observed metric; source={source}; interface={interface}; observable_now={observable_now}',
                '',
            )
        return 'PENDING', 'missing observed metric', ''

    observed = metrics[key]

    if key.startswith('max_') and isinstance(expected_value, (int, float)):
        passed = float(observed) <= float(expected_value)
        return ('PASS' if passed else 'FAIL'), f'{observed} <= {expected_value}', ''

    if key.startswith('min_') and isinstance(expected_value, (int, float)):
        passed = float(observed) >= float(expected_value)
        return ('PASS' if passed else 'FAIL'), f'{observed} >= {expected_value}', ''

    if key.startswith('should_') or key.endswith('_detected'):
        passed = bool(observed) == bool(expected_value)
        return ('PASS' if passed else 'FAIL'), f'{observed} == {expected_value}', ''

    passed = observed == expected_value
    return ('PASS' if passed else 'FAIL'), f'{observed} == {expected_value}', ''


def evaluate_scenario(path, metrics_by_scenario, contract=None):
    data = _load_mapping(path)
    scenario_id = data.get('scenario_id', path.stem)
    expected = data.get('expected', {}) or {}
    metrics = metrics_by_scenario.get(scenario_id, {})
    metric_sources = (contract or {}).get('metric_sources', {})

    rows = []
    for key, expected_value in expected.items():
        status, detail, note = _metric_status(
            key,
            expected_value,
            metrics,
            metric_sources.get(key),
        )
        rows.append({
            'scenario_id': scenario_id,
            'scenario_class': data.get('scenario_class', ''),
            'metric': key,
            'expected': expected_value,
            'observed': metrics.get(key, ''),
            'status': status,
            'detail': detail,
            'note': note,
        })
    return rows


def _load_metrics(path):
    if not path:
        return {}
    raw = _load_mapping(Path(path))
    if 'scenarios' in raw and isinstance(raw['scenarios'], dict):
        return raw['scenarios']
    return raw


def _default_contract_path():
    try:
        return Path(get_package_share_directory('mock_scenarios')) / 'config' / 'validation' / 'mock_data_contract.yaml'
    except PackageNotFoundError:
        return Path(__file__).resolve().parents[1] / 'config' / 'validation' / 'mock_data_contract.yaml'


def _print_text(rows):
    current = None
    for row in rows:
        if row['scenario_id'] != current:
            current = row['scenario_id']
            print(f"\n{current} [{row['scenario_class']}]")
        observed = row['observed'] if row['observed'] != '' else '-'
        print(
            f"  {row['status']:<7} {row['metric']}: "
            f"expected={row['expected']} observed={observed} {row['detail']}"
        )


def _write_json(rows, path):
    with open(path, 'w', encoding='utf-8') as stream:
        json.dump(rows, stream, indent=2, ensure_ascii=False)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='Evaluate scenario expected metrics against observed metrics.'
    )
    parser.add_argument('scenario', nargs='*', help='Scenario YAML files. Defaults to all scenarios.')
    parser.add_argument('--scenario-dir', help='Directory containing scenario YAML files.')
    parser.add_argument('--metrics', help='YAML/JSON observed metrics keyed by scenario_id.')
    parser.add_argument('--contract', default=str(_default_contract_path()), help='Metric observability contract YAML.')
    parser.add_argument('--fail-on-pending', action='store_true', help='Treat missing observed metrics as failure.')
    parser.add_argument('--json-out', help='Write machine-readable acceptance report.')
    args = parser.parse_args(argv)

    metrics_by_scenario = _load_metrics(args.metrics)
    contract = _load_mapping(Path(args.contract)) if args.contract else {}
    rows = []
    for path in _resolve_scenarios(args):
        rows.extend(evaluate_scenario(path, metrics_by_scenario, contract))

    _print_text(rows)
    if args.json_out:
        _write_json(rows, args.json_out)

    failed = [row for row in rows if row['status'] == 'FAIL']
    pending = [row for row in rows if row['status'] == 'PENDING']
    print(
        f"\nSUMMARY: {len(failed)} failed, "
        f"{sum(1 for row in rows if row['status'] == 'PASS')} passed, "
        f"{len(pending)} pending"
    )
    raise SystemExit(1 if failed or (args.fail_on_pending and pending) else 0)


if __name__ == '__main__':
    main(sys.argv[1:])
