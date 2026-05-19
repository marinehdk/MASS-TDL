import type { GateSSEEvent } from '../../types/gateStream';

interface Ros2TopologySvgProps { gates: GateSSEEvent[]; focusedGateId?: number | null; }

const NODES: { id: string; label: string; x: number; y: number; group: 'module' | 'infra' }[] = [
  { id: 'M1', label: 'M1 Envel', x: 185, y: 65, group: 'module' },
  { id: 'M2', label: 'M2 Predict', x: 315, y: 50, group: 'module' },
  { id: 'M3', label: 'M3 Assess', x: 445, y: 80, group: 'module' },
  { id: 'M4', label: 'M4 Mid-MPC', x: 520, y: 140, group: 'module' },
  { id: 'M5', label: 'M5 Planner', x: 520, y: 220, group: 'module' },
  { id: 'M6', label: 'M6 Control', x: 445, y: 280, group: 'module' },
  { id: 'M7', label: 'M7 Safety', x: 315, y: 310, group: 'module' },
  { id: 'M8', label: 'M8 Sensor', x: 185, y: 295, group: 'module' },
  { id: 'orch', label: 'Orch', x: 90, y: 100, group: 'infra' },
  { id: 'foxglove', label: 'Foxglove', x: 50, y: 180, group: 'infra' },
  { id: 'martin', label: 'Martin', x: 90, y: 260, group: 'infra' },
];

const STATUS_COLORS: Record<string, { stroke: string; fill: string }> = {
  ok:     { stroke: 'var(--c-stbd)',  fill: 'rgba(0,227,179,0.15)' },
  fail:   { stroke: 'var(--c-danger)', fill: 'rgba(248,81,73,0.15)' },
  warn:   { stroke: 'var(--c-warn)',   fill: 'rgba(240,183,47,0.15)' },
  unknown:{ stroke: 'var(--txt-3)',     fill: 'var(--bg-2)' },
};

export function Ros2TopologySvg({ gates, focusedGateId }: Ros2TopologySvgProps) {
  const gate2 = gates.find(g => g.gate_id === 2);
  const gate1 = gates.find(g => g.gate_id === 1);

  function nodeStatus(nodeId: string): string {
    const nodeDef = NODES.find(n => n.id === nodeId);
    if (nodeDef?.group === 'module') {
      if (gate2?.checks) {
        const check = gate2.checks.find(c => c.item?.toLowerCase().includes(nodeId.toLowerCase()));
        if (check) return check.status;
      }
      return gate2 ? (gate2.passed ? 'ok' : 'unknown') : 'unknown';
    }
    if (nodeDef?.group === 'infra') {
      if (gate1?.checks) {
        let keyword = nodeId;
        if (nodeId === 'orch') keyword = 'docker';
        const check = gate1.checks.find(c => c.item?.toLowerCase().includes(keyword));
        if (check) return check.status;
      }
      return gate1 ? (gate1.passed ? 'ok' : 'unknown') : 'unknown';
    }
    return 'unknown';
  }

  const cx = 300, cy = 180, rx = 180, ry = 45;

  return (
    <svg viewBox="0 0 600 400" style={{ width: '100%', height: '100%', background: 'var(--bg-0)' }}>
      <defs>
        <filter id="glow-stbd" x="-30%" y="-30%" width="160%" height="160%">
          <feGaussianBlur stdDeviation="6" result="blur" />
          <feComposite in="SourceGraphic" in2="blur" operator="over" />
        </filter>
        <filter id="glow-danger" x="-30%" y="-30%" width="160%" height="160%">
          <feGaussianBlur stdDeviation="8" result="blur" />
          <feComposite in="SourceGraphic" in2="blur" operator="over" />
        </filter>
      </defs>

      {/* Left Annotation */}
      <text x={20} y={30} textAnchor="start" fill="var(--txt-0)" fontSize={12} fontFamily="var(--f-mono)" fontWeight={600} letterSpacing="0.05em">DDS BUS INTERFACE</text>
      <text x={20} y={45} textAnchor="start" fill="var(--txt-3)" fontSize={9} fontFamily="var(--f-body)">ROS2 Star Topology Network</text>

      {/* Center Node */}
      <circle cx={cx} cy={cy} r={14} fill="var(--bg-2)" stroke="var(--txt-3)" strokeWidth={1.5} />
      <circle cx={cx} cy={cy} r={6} fill="var(--txt-3)" />

      {NODES.map(n => {
        const status = nodeStatus(n.id);
        const colors = STATUS_COLORS[status] || STATUS_COLORS.unknown;
        const isFailed = status === 'fail';
        const isOk = status === 'ok';
        
        // Emphasize based on focused gate
        const isFocusedGroup = (focusedGateId === 1 && n.group === 'infra') || (focusedGateId === 2 && n.group === 'module');
        const nodeRadius = isFailed ? 18 : (isFocusedGroup ? 16 : 12);
        
        // Line styling
        let lineStroke = 'var(--line-2)';
        let lineDash = undefined;
        let lineOpacity = 0.5;
        let strokeWidth = 1.5;

        if (isFailed) {
          lineStroke = 'var(--c-danger)';
          lineDash = '4 4';
          lineOpacity = 1;
          strokeWidth = 2;
        } else if (isOk && isFocusedGroup) {
          lineStroke = 'var(--c-stbd)';
          lineOpacity = 0.8;
          strokeWidth = 2;
        } else if (isOk) {
          lineStroke = 'var(--c-stbd)';
          lineOpacity = 0.3;
        }

        return (
          <g key={n.id} style={{ transition: 'all 0.3s ease' }}>
            <line x1={n.x} y1={n.y} x2={cx} y2={cy}
              stroke={lineStroke} strokeWidth={strokeWidth} strokeDasharray={lineDash} opacity={lineOpacity} />
              
            <circle cx={n.x} cy={n.y} r={nodeRadius} fill={colors.fill} stroke={colors.stroke} strokeWidth={isFocusedGroup ? 2 : 1.5} 
              filter={isFailed ? 'url(#glow-danger)' : (isOk && isFocusedGroup ? 'url(#glow-stbd)' : 'none')} />
              
            {/* Inner dot */}
            <circle cx={n.x} cy={n.y} r={3} fill={colors.stroke} opacity={isOk ? 1 : 0.3} />
            
            <text x={n.x} y={n.y + nodeRadius + 14} textAnchor="middle" fill={isFocusedGroup ? 'var(--txt-0)' : 'var(--txt-2)'} fontSize={10} fontFamily="var(--f-mono)" fontWeight={isFocusedGroup ? 600 : 400}>{n.label}</text>
            
            {isFailed && <text x={n.x + nodeRadius + 8} y={n.y - nodeRadius + 4} fill="var(--c-danger)" fontSize={16} fontWeight="bold" filter="url(#glow-danger)">✗</text>}
            {isOk && isFocusedGroup && <text x={n.x + nodeRadius + 6} y={n.y - nodeRadius + 6} fill="var(--c-stbd)" fontSize={10}>✓</text>}
          </g>
        );
      })}

      {/* Top Right Legend (Vertical Stack) */}
      <g transform="translate(520, 20)">
        {[{ status: 'ok', label: 'Healthy' }, { status: 'fail', label: 'Failed' }, { status: 'unknown', label: 'Pending' }].map((s, i) => {
          const c = STATUS_COLORS[s.status];
          return (
            <g key={s.status} transform={`translate(0, ${i * 18})`}>
              <circle cx={6} cy={6} r={4} fill={c.fill} stroke={c.stroke} strokeWidth={1} />
              <text x={16} y={9} textAnchor="start" fill="var(--txt-2)" fontSize={8.5} fontFamily="var(--f-body)">{s.label}</text>
            </g>
          );
        })}
      </g>
    </svg>
  );
}
