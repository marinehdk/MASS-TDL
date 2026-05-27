import type { GateSSEEvent } from '../../types/gateStream';

interface ContainerBoundarySvgProps {
  gates: GateSSEEvent[];
  gate6Result: GateSSEEvent | undefined;
  gate5Result: GateSSEEvent | undefined;
}

const DOER_MODULES = [
  { id: 'M1', name: 'M1 ODD' },
  { id: 'M2', name: 'M2 World' },
  { id: 'M3', name: 'M3 Mission' },
  { id: 'M4', name: 'M4 Behavior' },
  { id: 'M5', name: 'M5 Planner' },
  { id: 'M6', name: 'M6 Control' },
];

const STATUS_COLORS: Record<string, { stroke: string; fill: string; dot: string }> = {
  ok:      { stroke: 'var(--c-stbd)',  fill: 'rgba(0,227,179,0.06)',  dot: 'var(--c-stbd)' },
  fail:    { stroke: 'var(--c-danger)', fill: 'rgba(248,81,73,0.06)', dot: 'var(--c-danger)' },
  warn:    { stroke: 'var(--c-warn)',   fill: 'rgba(240,183,47,0.06)',   dot: 'var(--c-warn)' },
  unknown: { stroke: 'var(--line-2)',   fill: 'rgba(255,255,255,0.02)', dot: 'var(--txt-3)' },
};

export function ContainerBoundarySvg({ gates, gate6Result, gate5Result }: ContainerBoundarySvgProps) {
  const isolationPassed = gate6Result?.passed === true;
  const timePassed = gate5Result?.passed === true;
  const gate2 = gates.find(g => g.gate_id === 2);

  const getModuleStatus = (moduleId: string): string => {
    if (!gate2?.checks) return 'unknown';
    const check = gate2.checks.find((c: any) => c.item?.toLowerCase().includes(moduleId.toLowerCase()));
    return check ? check.status : 'unknown';
  };

  const getModuleLabel = (moduleId: string): string => {
    if (!gate2?.checks) return 'WAITING';
    const check = gate2.checks.find((c: any) => c.item?.toLowerCase().includes(moduleId.toLowerCase()));
    return check ? check.status.toUpperCase() : 'WAITING';
  };

  const borderColor = isolationPassed ? 'var(--c-stbd)' : 'var(--c-danger)';
  const borderAnim = isolationPassed ? undefined : 'flicker-border 1.5s ease-in-out infinite';

  return (
    <svg viewBox="0 0 700 360" style={{ width: '100%', height: '100%', background: 'var(--bg-0)' }}>
      <defs>
        <filter id="svg-glow-stbd" x="-20%" y="-20%" width="140%" height="140%">
          <feGaussianBlur stdDeviation="5" result="blur" />
          <feComposite in="SourceGraphic" in2="blur" operator="over" />
        </filter>
        <filter id="svg-glow-danger" x="-20%" y="-20%" width="140%" height="140%">
          <feGaussianBlur stdDeviation="7" result="blur" />
          <feComposite in="SourceGraphic" in2="blur" operator="over" />
        </filter>
        <style>{`
          @keyframes flicker-border {
            0% { opacity: 1; }
            50% { opacity: 0.4; }
            100% { opacity: 1; }
          }
        `}</style>
      </defs>

      {/* Title & Specs */}
      <g transform="translate(20, 25)">
        <text x={0} y={0} fill="var(--txt-0)" fontSize={12} fontFamily="var(--f-disp)" fontWeight={700} letterSpacing="0.05em">
          Doer-Checker 物理安全隔离沙箱
        </text>
        <text x={0} y={15} fill="var(--txt-3)" fontSize={9} fontFamily="var(--f-body)">
          IEC 61508 SIL 2 &amp; SOTIF 共因失效物理隔离度验证
        </text>
      </g>

      {/* Doer Container (M1-M6) */}
      <g>
        <rect x={20} y={55} width={400} height={250} rx={8}
          fill="rgba(255,255,255,0.01)" stroke={borderColor} strokeWidth={1.5}
          style={{ animation: borderAnim, transition: 'all 0.3s' }}
          filter={!isolationPassed && gate6Result ? 'url(#svg-glow-danger)' : 'none'} />
        
        {/* Container Header Banner */}
        <path d="M 20 63 A 8 8 0 0 1 28 55 L 412 55 A 8 8 0 0 1 420 63 L 420 80 L 20 80 Z" fill="rgba(255,255,255,0.03)" />
        <text x={35} y={72} fill="var(--txt-1)" fontSize={10} fontFamily="var(--f-disp)" fontWeight={700}>
          Doer Container (M1-M6 主运行容器)
        </text>
        <text x={405} y={71} textAnchor="end" fill={borderColor} fontSize={8.5} fontFamily="var(--f-mono)" fontWeight={700}>
          {isolationPassed ? 'SECURED' : 'COMPROMISED'}
        </text>

        {/* Modules Render */}
        {DOER_MODULES.map((m, i) => {
          const col = i % 3;
          const row = Math.floor(i / 3);
          const nx = 40 + col * 125;
          const ny = 95 + row * 100;
          const status = getModuleStatus(m.id);
          const colors = STATUS_COLORS[status] || STATUS_COLORS.unknown;

          return (
            <g key={m.id} transform={`translate(${nx}, ${ny})`}>
              {/* Card background */}
              <rect x={0} y={0} width={110} height={80} rx={6}
                fill="var(--bg-1)" stroke={colors.stroke} strokeWidth={1} />
              
              {/* Header inside card */}
              <rect x={0} y={0} width={110} height={22} rx={6} fill="rgba(255,255,255,0.02)" />
              <line x1={0} y1={22} x2={110} y2={22} stroke="rgba(255,255,255,0.05)" strokeWidth={1} />
              
              {/* LED status indicator dot */}
              <circle cx={12} cy={11} r={3.5} fill={colors.dot} />
              
              <text x={24} y={14} fill="var(--txt-0)" fontSize={9.5} fontFamily="var(--f-disp)" fontWeight={700}>
                {m.id}
              </text>
              <text x={100} y={14} textAnchor="end" fill="var(--txt-3)" fontSize={7.5} fontFamily="var(--f-mono)">
                {getModuleLabel(m.id)}
              </text>
              
              <text x={12} y={42} fill="var(--txt-1)" fontSize={9.5} fontFamily="var(--f-body)">
                {m.name}
              </text>
              <text x={12} y={58} fill="var(--txt-3)" fontSize={8} fontFamily="var(--f-mono)">
                {status === 'ok' ? 'State: Active' : status === 'warn' ? 'State: Unspec' : 'State: Pending'}
              </text>
            </g>
          );
        })}
      </g>

      {/* Separation Barrier Line */}
      <g>
        <line x1={445} y1={55} x2={445} y2={305}
          stroke={borderColor} strokeWidth={1}
          strokeDasharray={isolationPassed ? '4 4' : '2 2'}
          opacity={0.6} />
        <rect x={415} y={160} width={60} height={16} rx={4}
          fill="var(--bg-0)" stroke={borderColor} strokeWidth={1} />
        <text x={445} y={171} textAnchor="middle" fill={borderColor} fontSize={8} fontFamily="var(--f-mono)" fontWeight={700}>
          {isolationPassed ? 'ISOLATED' : 'SHARED'}
        </text>
      </g>

      {/* Checker Container (M7) */}
      <g>
        <rect x={470} y={55} width={210} height={130} rx={8}
          fill="rgba(255,255,255,0.01)" stroke={borderColor} strokeWidth={1.5}
          style={{ animation: borderAnim, transition: 'all 0.3s' }}
          filter={!isolationPassed && gate6Result ? 'url(#svg-glow-danger)' : 'none'} />
        
        {/* Container Header Banner */}
        <path d="M 470 63 A 8 8 0 0 1 478 55 L 672 55 A 8 8 0 0 1 680 63 L 680 80 L 470 80 Z" fill="rgba(255,255,255,0.03)" />
        <text x={485} y={72} fill="var(--txt-0)" fontSize={10} fontFamily="var(--f-disp)" fontWeight={700}>
          Checker (M7 独立安全容器)
        </text>

        {/* M7 Card */}
        <g transform="translate(485, 95)">
          <rect x={0} y={0} width={180} height={75} rx={6}
            fill="var(--bg-1)" stroke={borderColor} strokeWidth={1} />
          <rect x={0} y={0} width={180} height={22} rx={6} fill="rgba(255,255,255,0.02)" />
          <line x1={0} y1={22} x2={180} y2={22} stroke="rgba(255,255,255,0.05)" strokeWidth={1} />
          
          <circle cx={12} cy={11} r={3.5} fill={borderColor} />
          <text x={24} y={14} fill="var(--txt-0)" fontSize={9.5} fontFamily="var(--f-disp)" fontWeight={700}>
            M7 Supervisor
          </text>
          
          <text x={12} y={42} fill="var(--txt-1)" fontSize={9.5} fontFamily="var(--f-body)">
            Safety Supervisor
          </text>
          <text x={12} y={58} fill="var(--txt-3)" fontSize={8} fontFamily="var(--f-mono)">
            {isolationPassed ? 'Status: Standalone Process' : 'Status: Process Violated'}
          </text>
        </g>
      </g>

      {/* VETO Arrow */}
      <g>
        <line x1={420} y1={120} x2={465} y2={95}
          stroke={borderColor} strokeWidth={1.5} markerEnd="url(#arrow)" />
        <text x={442} y={110} textAnchor="middle" fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-mono)" fontWeight={600}>
          VETO →
        </text>
      </g>

      {/* Gate 5 Alert */}
      {!timePassed && gate5Result && (
        <g transform="translate(470, 200)">
          <text x={0} y={10} fill="var(--c-warn)" fontSize={10} fontFamily="var(--f-body)" fontWeight={600}>
            ⏱ 时钟飘移校验未通过
          </text>
          <text x={0} y={23} fill="var(--txt-3)" fontSize={8.5} fontFamily="var(--f-mono)">
            {gate5Result.rationale}
          </text>
        </g>
      )}

      {/* Gate 6 Isolation Violation Explanation */}
      {!isolationPassed && gate6Result && (
        <g transform="translate(470, 238)">
          <text x={0} y={10} fill="var(--c-danger)" fontSize={11} fontFamily="var(--f-disp)" fontWeight={700}>
            FATAL: Doer-Checker 物理隔离失效
          </text>
          {/* Explanatory text wrap */}
          <text x={0} y={26} fill="var(--txt-2)" fontSize={8.5} fontFamily="var(--f-body)">
            原因: 检测到 M7 进程运行在 Doer 共享的
          </text>
          <text x={0} y={38} fill="var(--txt-2)" fontSize={8.5} fontFamily="var(--f-body)">
            容器或 PID 空间内，未满足物理独立性要求。
          </text>
          <text x={0} y={50} fill="var(--txt-3)" fontSize={8} fontFamily="var(--f-mono)">
            {gate6Result.rationale}
          </text>
        </g>
      )}

      {/* Bottom Legend */}
      <g transform="translate(20, 335)">
        <rect x={0} y={0} width={10} height={10} rx={2} fill="rgba(0,227,179,0.06)" stroke="var(--c-stbd)" strokeWidth={1} />
        <text x={16} y={8} fill="var(--txt-2)" fontSize={8.5} fontFamily="var(--f-body)">隔离正常</text>
        <rect x={90} y={0} width={10} height={10} rx={2} fill="rgba(248,81,73,0.06)" stroke="var(--c-danger)" strokeWidth={1} />
        <text x={106} y={8} fill="var(--txt-2)" fontSize={8.5} fontFamily="var(--f-body)">隔离失败</text>
      </g>
    </svg>
  );
}
