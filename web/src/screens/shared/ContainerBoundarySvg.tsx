import type { GateSSEEvent } from '../../types/gateStream';

interface ContainerBoundarySvgProps {
  gate6Result: GateSSEEvent | undefined;
  gate5Result: GateSSEEvent | undefined;
}

const DOER_MODULES = ['M1', 'M2', 'M3', 'M4', 'M5', 'M6'];

export function ContainerBoundarySvg({ gate6Result, gate5Result }: ContainerBoundarySvgProps) {
  const isolationPassed = gate6Result?.passed === true;
  const timePassed = gate5Result?.passed === true;

  const borderColor = isolationPassed ? 'var(--c-stbd)' : 'var(--c-danger)';
  const borderAnim = isolationPassed ? undefined : 'flicker 0.8s ease-in-out infinite';

  return (
    <svg viewBox="0 0 700 350" style={{ width: '100%', height: '100%', background: 'var(--bg-0)' }}>
      <rect x={20} y={30} width={400} height={280} rx={8}
        fill="rgba(0,227,179,0.03)" stroke={borderColor} strokeWidth={2} style={{ animation: borderAnim }} />
      <text x={220} y={50} textAnchor="middle" fill="var(--txt-0)" fontSize={12} fontFamily="var(--f-disp)">Doer Container (M1\u2013M6)</text>

      {DOER_MODULES.map((m, i) => {
        const col = i % 3; const row = Math.floor(i / 3);
        const nx = 80 + col * 130, ny = 80 + row * 80;
        return (
          <g key={m}>
            <rect x={nx - 20} y={ny - 12} width={80} height={28} rx={4}
              fill="var(--bg-2)" stroke="var(--line-2)" strokeWidth={1} />
            <text x={nx + 20} y={ny + 6} textAnchor="middle" fill="var(--txt-1)" fontSize={10} fontFamily="var(--f-mono)">{m}</text>
          </g>
        );
      })}

      <rect x={470} y={30} width={180} height={120} rx={8}
        fill="rgba(248,81,73,0.03)" stroke={borderColor} strokeWidth={2} style={{ animation: borderAnim }} />
      <text x={560} y={50} textAnchor="middle" fill="var(--txt-0)" fontSize={12} fontFamily="var(--f-disp)">Checker (M7)</text>
      <rect x={520} y={60} width={80} height={28} rx={4} fill="var(--bg-2)" stroke="var(--line-2)" strokeWidth={1} />
      <text x={560} y={78} textAnchor="middle" fill="var(--txt-1)" fontSize={10} fontFamily="var(--f-mono)">M7</text>

      <line x1={420} y1={120} x2={470} y2={90} stroke={isolationPassed ? 'var(--c-stbd)' : 'var(--c-danger)'}
        strokeWidth={2} markerEnd="url(#arrow)" />
      <defs>
        <marker id="arrow" viewBox="0 0 10 10" refX={9} refY={5} markerWidth={6} markerHeight={6} orient="auto">
          <path d="M0,0 L10,5 L0,10 Z" fill={isolationPassed ? 'var(--c-stbd)' : 'var(--c-danger)'} />
        </marker>
      </defs>
      <text x={445} y={115} textAnchor="middle" fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-mono)">VETO \u2192</text>

      {!timePassed && gate5Result && (
        <g transform="translate(470, 170)">
          <text x={0} y={0} fill="var(--c-warn)" fontSize={11} fontFamily="var(--f-body)">{'\u23F1'} Clock Drift</text>
          <text x={0} y={16} fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-mono)">{gate5Result.rationale}</text>
        </g>
      )}

      {!isolationPassed && (
        <g transform="translate(470, 220)">
          <text x={0} y={0} fill="var(--c-danger)" fontSize={12} fontFamily="var(--f-disp)" fontWeight="bold">FATAL</text>
          <text x={0} y={16} fill="var(--c-danger)" fontSize={10} fontFamily="var(--f-body)">{'\u7269\u7406\u9694\u79BB\u88AB\u7834\u574F'}</text>
        </g>
      )}

      <g transform="translate(20, 330)">
        <rect x={0} y={0} width={12} height={12} rx={2} fill="none" stroke="var(--c-stbd)" strokeWidth={2} />
        <text x={18} y={10} fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-body)">{'\u9694\u79BB\u6B63\u5E38'}</text>
        <rect x={100} y={0} width={12} height={12} rx={2} fill="none" stroke="var(--c-danger)" strokeWidth={2} />
        <text x={118} y={10} fill="var(--txt-2)" fontSize={9} fontFamily="var(--f-body)">{'\u9694\u79BB\u5931\u8D25'}</text>
      </g>
    </svg>
  );
}
