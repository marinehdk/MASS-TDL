from pathlib import Path
import argparse
import csv
import json
import sys

import yaml


def _load_yaml(path):
    if not path or not Path(path).exists():
        return {}
    with open(path, 'r', encoding='utf-8') as stream:
        return yaml.safe_load(stream) or {}


def _load_json(path):
    if not path or not Path(path).exists():
        return []
    with open(path, 'r', encoding='utf-8') as stream:
        return json.load(stream) or []


def _read_suite_results(path):
    with open(path, 'r', encoding='utf-8') as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def _scenario_metrics(run_dir, scenario_id):
    run_path = Path(run_dir)
    for name in ('metrics_merged.yaml', 'metrics.yaml', 'observability_metrics.yaml'):
        report = _load_yaml(run_path / name)
        scenarios = report.get('scenarios', {}) or {}
        if scenario_id in scenarios:
            return scenarios[scenario_id]
    return {}


def _acceptance_counts(rows):
    counts = {'PASS': 0, 'FAIL': 0, 'PENDING': 0, 'INFO': 0}
    for row in rows:
        status = row.get('status', 'INFO')
        counts[status] = counts.get(status, 0) + 1
    return counts


def _status(row, counts, metrics):
    try:
        rc = int(row.get('rc', '0'))
    except ValueError:
        rc = 1
    if rc != 0:
        if counts.get('FAIL', 0) > 0:
            return 'ACCEPTANCE_FAILED'
        return 'RUN_OR_POSTCHECK_FAILED'
    if not metrics.get('final_waypoint_reached', False):
        return 'A_TO_B_NOT_PROVEN'
    if counts.get('FAIL', 0) > 0:
        return 'ACCEPTANCE_FAILED'
    if counts.get('PENDING', 0) > 0:
        return 'PARTIAL_OBSERVABILITY'
    return 'PASS_LIMITED'


def _fmt(value, digits=2):
    if value == '' or value is None:
        return '-'
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, (int, float)):
        return f'{float(value):.{digits}f}'
    return str(value)


def _metric_names(rows, status):
    return [
        row.get('metric', 'unknown')
        for row in rows
        if row.get('status') == status
    ]


def build_report(rows):
    lines = []
    lines.append('# MASS ADAS 现有系统能力边界报告')
    lines.append('')
    lines.append('## 结论摘要')
    lines.append('')
    lines.append('本报告基于 mock data 场景运行结果生成。所有场景参数、安全限值和任务 gate 均按 C 级可信度处理，不能作为实船能力声明。')
    lines.append('')
    lines.append('| 场景 | 类别 | 运行结论 | 到达终点 | PASS | FAIL | PENDING | 最小终点误差(m) | 最大横向误差(m) | 阶段 | 安全状态 |')
    lines.append('|---|---|---|---:|---:|---:|---:|---:|---:|---|---|')

    detail_blocks = []
    for row in rows:
        scenario_file = row.get('scenario_file', '')
        scenario = _load_yaml(scenario_file)
        scenario_id = row.get('scenario_id') or scenario.get('scenario_id', Path(scenario_file).stem)
        run_dir = row.get('run_dir', '')
        metrics = _scenario_metrics(run_dir, scenario_id)
        acceptance = _load_json(Path(run_dir) / 'acceptance.json')
        counts = _acceptance_counts(acceptance)
        status = _status(row, counts, metrics)
        phases = ','.join(metrics.get('mission_phases_seen', []) or [])
        safety_statuses = ','.join(metrics.get('safety_statuses_seen', []) or [])
        lines.append(
            '| {sid} | {cls} | {status} | {reached} | {pass_count} | {fail_count} | {pending_count} | {min_final} | {cte} | {phases} | {safety} |'.format(
                sid=scenario_id,
                cls=scenario.get('scenario_class', '-'),
                status=status,
                reached=_fmt(metrics.get('final_waypoint_reached', False), 0),
                pass_count=counts.get('PASS', 0),
                fail_count=counts.get('FAIL', 0),
                pending_count=counts.get('PENDING', 0),
                min_final=_fmt(metrics.get('min_final_position_error_m')),
                cte=_fmt(metrics.get('max_cross_track_error_m')),
                phases=phases or '-',
                safety=safety_statuses or '-',
            )
        )

        failures = _metric_names(acceptance, 'FAIL')
        pending = _metric_names(acceptance, 'PENDING')
        detail_blocks.extend([
            f'### {scenario_id}',
            '',
            f'- 验证目标：{scenario.get("validation_objective", "-")}',
            f'- 日志目录：`{run_dir or "-"}`',
            f'- 运行结论：`{status}`',
            f'- 最终航点到达：`{_fmt(metrics.get("final_waypoint_reached", False), 0)}`',
            f'- 最小终点误差：`{_fmt(metrics.get("min_final_position_error_m"))} m`',
            f'- 最大横向误差：`{_fmt(metrics.get("max_cross_track_error_m"))} m`',
            f'- 最大艏向误差：`{_fmt(metrics.get("max_heading_error_deg"))} deg`',
            f'- Mission phases：`{phases or "-"}`',
            f'- Safety statuses：`{safety_statuses or "-"}`',
            f'- 失败指标：`{", ".join(failures) if failures else "-"}`',
            f'- 待补充观测指标：`{", ".join(pending) if pending else "-"}`',
            '',
        ])

    lines.append('')
    lines.append('## 判读原则')
    lines.append('')
    lines.append('- `PASS_LIMITED` 只表示当前 mock 验收项通过，不代表实船能力已验证。')
    lines.append('- `A_TO_B_NOT_PROVEN` 表示未证明从 A 点运行到 B 点，即使部分横向误差或安全状态指标可能通过。')
    lines.append('- `PARTIAL_OBSERVABILITY` 表示主要链路可运行，但仍有验收指标没有自动观测来源。')
    lines.append('- `ACCEPTANCE_FAILED` 表示已有自动观测指标未满足场景期望。')
    lines.append('')
    lines.append('## 分场景细节')
    lines.append('')
    lines.extend(detail_blocks)
    return '\n'.join(lines).rstrip() + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description='Generate a Markdown report for a scenario validation suite.')
    parser.add_argument('--suite-results', required=True, help='TSV generated by wsl_ros2_run_suite.sh.')
    parser.add_argument('--output', required=True, help='Markdown report path.')
    args = parser.parse_args(argv)

    rows = _read_suite_results(Path(args.suite_results))
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(build_report(rows), encoding='utf-8')


if __name__ == '__main__':
    main(sys.argv[1:])
