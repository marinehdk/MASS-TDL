import type { GateSSEEvent } from '../../types/gateStream';

interface Ros2TopologySvgProps { gates: GateSSEEvent[] }

const NODES: { id: string; label: string; x: number; y: number; group: 'module' | 'infra' }[] = [
  { id: 'M1', label: 'M1', x: 120, y: 60, group: 'module' },
  { id: 'M2', label: 'M2', x: 220, y: 40, group: 'module' },
  { id: 'M3', label: 'M3', x: 340, y: 40, group: 'module' },
  { id: 'M4', label: 'M4', x: 460, y: 60, group: 'module' },
  { id: 'M5', label: 'M5', x: 120, y: 300, group: 'module' },
  { id: 'M6', label: 'M6', x: 220, y: 320, group: 'module' },
  { id: 'M7', label: 'M7', x: 340, y: 320, group: 'module' },
  { id: 'M8', label: 'M8', x: 460, y: 300, group: 'module' },
  { id: 'orch', label: 'Orch', x: 30, y: 180, group: 'infra' },
  { id: 'foxglove', label: 'Foxglove', x: 520, y: 140, group: 'infra' },
  { id: 'martin', label: 'Martin', x: 520, y: 240, group: 'infra' },
];

const STATUS_COLORS: Record<string, { stroke: string; fill: string }> = {
  ok:     { stroke: 'var(--c-stbd)',  fill: 'rgba(0,227,179,0.15)' },
  fail:   { stroke: 'var(--c-danger)', fill: 'rgba(248,81,73,0.15)' },
  warn:   { stroke: 'var(--c-warn)',   fill: 'rgba(240,183,47,0.15)' },
  unknown:{ stroke: 'var(--txt-3)',     fill: 'var(--bg-2)' },
};

export function Ros2TopologySvg({ gates }: Ros2TopologySvgProps) {
  const gate2 = gates.find(g => g.gate_id === 2);
  const gate1 = gates.find(g => g.gate_id === 1);

  function nodeStatus(nodeId: string): string {
    if (gate2?.checks) {
      const check = gate2.checks.find(c => c.item?.toLowerCase().includes(nodeId.toLowerCase()));
      if (check) return check.status;
    }
    if (nodeId === 'orch' || nodeId === 'foxglove' || nodeId === 'martin') {
      if (gate1?.passed) return 'ok';
      return 'unknown';
    }
    return 'unknown';
  }

  const cx = 300, cy = 180, rx = 160, ry = 45;

  return (
    <svg viewBox="0 0 600 400" style={{ width: '100%', height: '100%', background: 'var(--bg-0)' }}>
      <ellipse cx={cx} cy={cy} rx={rx} ry={ry} fill="none" stroke="var(--line-2)" strokeWidth={1.5} strokeDasharray="6 3" />
      <text x={cx} y={cy + 4} textAnchor="middle" fill="var(--txt-2)" fontSize={10} fontFamily="var(--f-mono)">DDS Bus</text>

      {NODES.map(n => {
        const status = nodeStatus(n.id);
        const colors = STATUS_COLORS[status] || STATUS_COLORS.unknown;
        const isFailed = status === 'fail';
        return (
          <g key={n.id}>
            <line x1={n.x} y1={n.y} x2={cx} y2={cy}
              stroke={isFailed ? 'var(--c-danger)' : 'var(--line-2)'}
              strokeWidth={1.5} strokeDasharray={isFailed ? '4 3' : undefined} />
            <circle cx={n.x} cy={n.y} r={isFailed ? 16 : 13} fill={colors.fill} stroke={colors.stroke} strokeWidth={2} />
            <text x={n.x} y={n.y + 4} textAnchor="middle" fill={colors.stroke} fontSize={9} fontFamily="var(--f-mono)">{n.label}</text>
            {isFailed && <text x={n.x + 16} y={n.y - 10} fill="var(--c-danger)" fontSize={14}>\u2717</text>}
          </g>
        );
      })}

      <g transform="translate(10, 370)">
        {[{ status: 'ok', label: 'Healthy' }, { status: 'fail', label: 'Failed' }, { status: 'unknown', label: 'Unknown' }].map((s, i) => {
          const c = STATUS_COLORS[s.status];
          return (
            <g key={s.status} transform={`translate(${i * 110}, 0)`}>
              <circle cx={6} cy={6} r={5} fill={c.fill} stroke={c.stroke} strokeWidth={1.5} />
              <text x={16} y={10} fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-body)">{s.label}</text>
            </g>
          );
        })}
      </g>
    </svg>
  );
}
