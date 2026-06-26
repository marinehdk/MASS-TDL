from pathlib import Path
import argparse
import sys

import yaml


def _load_yaml(path):
    with open(path, 'r', encoding='utf-8') as stream:
        return yaml.safe_load(stream) or {}


def merge_reports(paths):
    merged = {'scenarios': {}}
    for path in paths:
        raw = _load_yaml(path)
        scenarios = raw.get('scenarios', raw)
        for scenario_id, metrics in scenarios.items():
            if not isinstance(metrics, dict):
                continue
            merged['scenarios'].setdefault(scenario_id, {}).update(metrics)
    return merged


def main(argv=None):
    parser = argparse.ArgumentParser(description='Merge scenario metrics YAML files.')
    parser.add_argument('metrics', nargs='+', help='Metrics YAML files, merged left to right.')
    parser.add_argument('--output', required=True, help='Output merged YAML.')
    args = parser.parse_args(argv)

    report = merge_reports([Path(item) for item in args.metrics])
    with open(args.output, 'w', encoding='utf-8') as stream:
        yaml.safe_dump(report, stream, sort_keys=True)


if __name__ == '__main__':
    main(sys.argv[1:])
