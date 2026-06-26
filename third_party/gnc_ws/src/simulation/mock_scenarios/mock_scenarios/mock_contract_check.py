from pathlib import Path
import argparse
import json
import sys

import yaml


TEXT_KEYS = {'pass_fail_basis'}


def _load_yaml(path):
    with open(path, 'r', encoding='utf-8') as stream:
        return yaml.safe_load(stream) or {}


def _default_scenarios_root():
    return Path(__file__).resolve().parents[1] / 'config' / 'scenarios'


def _default_contract_path():
    return Path(__file__).resolve().parents[1] / 'config' / 'validation' / 'mock_data_contract.yaml'


def _default_policy_files():
    paths = []
    try:
        from ament_index_python.packages import get_package_share_directory
        safety_config = Path(get_package_share_directory('safety_supervisor')) / 'config' / 'safety_limits.yaml'
        paths.append(safety_config)
        mission_config = Path(get_package_share_directory('mission_supervisor')) / 'config' / 'mission_gates.yaml'
        paths.append(mission_config)
    except Exception:
        pass

    repo_safety_config = (
        Path(__file__).resolve().parents[4]
        / 'src'
        / 'safety'
        / 'safety_supervisor'
        / 'config'
        / 'safety_limits.yaml'
    )
    paths.append(repo_safety_config)
    repo_mission_config = (
        Path(__file__).resolve().parents[4]
        / 'src'
        / 'mission'
        / 'mission_supervisor'
        / 'config'
        / 'mission_gates.yaml'
    )
    paths.append(repo_mission_config)

    unique = []
    seen = set()
    for path in paths:
        resolved = str(path.resolve())
        if path.exists() and resolved not in seen:
            unique.append(path)
            seen.add(resolved)
    return unique


def _resolve_scenarios(args):
    if args.scenario:
        return [Path(item) for item in args.scenario]
    root = Path(args.scenario_dir) if args.scenario_dir else _default_scenarios_root()
    return sorted(root.glob('*.yaml'))


def _resolve_policy_files(args):
    if args.policy_file:
        return [Path(item) for item in args.policy_file]
    return _default_policy_files()


def _check_data_policy(rows, item_id, path, policy, contract):
    required_policy = contract.get('data_policy', {}) or {}

    def add(status, item, detail):
        rows.append({
            'scenario_id': item_id,
            'file': str(path),
            'status': status,
            'item': item,
            'detail': detail,
        })

    required_source = required_policy.get('required_parameter_source')
    if policy.get('parameter_source') != required_source:
        add('FAIL', 'data_policy.parameter_source',
            f"expected {required_source!r}, observed {policy.get('parameter_source')!r}")
    else:
        add('PASS', 'data_policy.parameter_source', required_source)

    required_confidence = required_policy.get('required_parameter_confidence')
    if policy.get('parameter_confidence') != required_confidence:
        add('FAIL', 'data_policy.parameter_confidence',
            f"expected {required_confidence!r}, observed {policy.get('parameter_confidence')!r}")
    else:
        add('PASS', 'data_policy.parameter_confidence', required_confidence)

    if not policy.get('real_data_transition'):
        add('FAIL', 'data_policy.real_data_transition', 'missing transition rule')
    else:
        add('PASS', 'data_policy.real_data_transition', 'present')


def check_scenario(path, contract):
    data = _load_yaml(path)
    scenario_id = data.get('scenario_id', path.stem)
    expected = data.get('expected', {}) or {}
    policy = data.get('data_policy', {}) or {}
    metric_sources = contract.get('metric_sources', {}) or {}

    rows = []

    def add(status, item, detail):
        rows.append({
            'scenario_id': scenario_id,
            'file': str(path),
            'status': status,
            'item': item,
            'detail': detail,
        })

    _check_data_policy(rows, scenario_id, path, policy, contract)

    for metric in expected:
        if metric in TEXT_KEYS:
            continue
        source = metric_sources.get(metric)
        if not source:
            add('FAIL', f'metric_sources.{metric}', 'missing metric source in contract')
            continue
        interface = source.get('interface')
        if not interface:
            add('FAIL', f'metric_sources.{metric}.interface', 'missing interface')
        else:
            observable = source.get('observable_now')
            add('PASS', f'metric_sources.{metric}', f"interface={interface}, observable_now={observable}")

    return rows


def check_policy_file(path, contract):
    data = _load_yaml(path)
    rows = []
    item_id = f"policy:{path.stem}"
    _check_data_policy(rows, item_id, path, data.get('data_policy', {}) or {}, contract)
    return rows


def _print_rows(rows):
    current = None
    for row in rows:
        if row['scenario_id'] != current:
            current = row['scenario_id']
            print(f"\n{current}")
        print(f"  {row['status']:<5} {row['item']}: {row['detail']}")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='Check mock scenario parameter confidence and observability contract.'
    )
    parser.add_argument('scenario', nargs='*', help='Scenario YAML files. Defaults to all scenarios.')
    parser.add_argument('--scenario-dir', help='Directory containing scenario YAML files.')
    parser.add_argument('--contract', default=str(_default_contract_path()), help='Mock data contract YAML.')
    parser.add_argument(
        '--policy-file',
        action='append',
        help='Additional YAML file whose data_policy must comply with the mock-data contract.',
    )
    parser.add_argument('--json-out', help='Write machine-readable report.')
    args = parser.parse_args(argv)

    contract = _load_yaml(Path(args.contract))
    rows = []
    for path in _resolve_scenarios(args):
        rows.extend(check_scenario(path, contract))
    for path in _resolve_policy_files(args):
        rows.extend(check_policy_file(path, contract))

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
