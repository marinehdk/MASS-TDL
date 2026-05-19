import type { GateSSEEvent } from '../../types/gateStream';

const SIX_GATE_LABELS: Record<number, string> = {
  1: '系统物理就绪', 2: '模块脉搏健康', 3: '场景与环境一致',
  4: '数据源与模型就绪', 5: '时基严密性验证', 6: '架构物理隔离',
};

interface GateSequencerProps {
  gates: GateSSEEvent[];
  streaming: boolean;
  focusedGateId: number | null;
  onGateSelect: (gateId: number) => void;
  verdict: 'GO' | 'NO-GO' | null;
}

export function GateSequencer({ gates, streaming, focusedGateId, onGateSelect, verdict }: GateSequencerProps) {
  const gateMap = new Map(gates.map(g => [g.gate_id, g]));
  const verdictBg = verdict === 'GO' ? 'var(--c-stbd)' : verdict === 'NO-GO' ? 'rgba(248,81,73,0.15)' : 'rgba(255,255,255,0.05)';
  const verdictBorder = verdict === 'GO' ? '1px solid var(--c-stbd)' : verdict === 'NO-GO' ? '1px solid var(--c-danger)' : '1px solid var(--line-2)';
  const verdictColor = verdict === 'GO' ? '#000' : verdict === 'NO-GO' ? 'var(--c-danger)' : 'var(--txt-3)';
  const verdictLabel = verdict ?? (streaming ? 'CHECKING' : 'IDLE');

  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      background: 'rgba(10, 15, 24, 0.95)',
      backdropFilter: 'blur(16px)',
      borderRight: '1px solid var(--line-2)',
      padding: '20px 14px',
      gap: 16
    }}>
      {/* Centered Brand Header matching Screen 1 sidebar */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8, padding: '0 4px 10px', borderBottom: '1px solid var(--line-1)' }}>
        <div style={{ width: 4, height: 14, background: 'var(--c-phos)', borderRadius: 2 }} />
        <span style={{ fontFamily: 'var(--f-disp)', fontSize: 15, fontWeight: 700, color: 'var(--txt-1)', letterSpacing: '0.2em' }}>
          安全门控
        </span>
      </div>

      {/* Sequencer List (Marquee/跑马灯 glow style) */}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 10, overflowY: 'auto' }}>
        {[1, 2, 3, 4, 5, 6].map(gateId => {
          const result = gateMap.get(gateId);
          const isFocused = focusedGateId === gateId;
          const isPending = !result && streaming;
          const isRunning = isPending && (gates.length + 1 === gateId);
          const isPassed = result?.passed === true;
          const isFailed = result?.passed === false;

          let statusText = 'WAITING';
          let glowColor = 'transparent';
          let ringColor = 'var(--line-2)';
          let textColor = 'var(--txt-2)';

          if (isPassed) {
            statusText = 'PASSED';
            glowColor = 'rgba(0, 227, 179, 0.1)';
            ringColor = 'var(--c-stbd)';
            textColor = 'var(--txt-0)';
          } else if (isFailed) {
            statusText = 'FAILED';
            glowColor = 'rgba(248, 81, 73, 0.1)';
            ringColor = 'var(--c-danger)';
            textColor = 'var(--txt-0)';
          } else if (isRunning) {
            statusText = 'CHECKING';
            glowColor = 'rgba(240, 183, 47, 0.15)';
            ringColor = 'var(--c-warn)';
            textColor = 'var(--txt-0)';
          }

          if (isFocused) {
            glowColor = isPassed ? 'rgba(0, 227, 179, 0.25)' : isFailed ? 'rgba(248, 81, 73, 0.25)' : 'rgba(91, 192, 190, 0.2)';
          }

          return (
            <div
              key={gateId}
              onClick={() => onGateSelect(gateId)}
              style={{
                display: 'flex',
                flexDirection: 'column',
                padding: '12px 14px',
                cursor: 'pointer',
                borderRadius: 8,
                background: isFocused ? 'rgba(255,255,255,0.03)' : 'rgba(0,0,0,0.15)',
                border: `1px solid ${isFocused ? 'var(--c-phos)' : ringColor}`,
                boxShadow: isFocused || isRunning ? `0 0 10px ${glowColor}` : 'none',
                transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
                animation: isRunning ? 'pulse 1.5s ease-in-out infinite' : 'none',
                position: 'relative',
                overflow: 'hidden'
              }}
            >
              {/* Running LED indicator */}
              {isRunning && (
                <div style={{
                  position: 'absolute', top: 0, left: 0, bottom: 0, width: 3,
                  background: 'var(--c-warn)', boxShadow: '0 0 8px var(--c-warn)'
                }} />
              )}
              {isPassed && (
                <div style={{
                  position: 'absolute', top: 0, left: 0, bottom: 0, width: 3,
                  background: 'var(--c-stbd)', boxShadow: '0 0 8px var(--c-stbd)'
                }} />
              )}
              {isFailed && (
                <div style={{
                  position: 'absolute', top: 0, left: 0, bottom: 0, width: 3,
                  background: 'var(--c-danger)', boxShadow: '0 0 8px var(--c-danger)'
                }} />
              )}

              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, fontWeight: 700, color: 'var(--txt-3)', letterSpacing: '0.05em' }}>
                  GATE 0{gateId}
                </span>
                <span style={{
                  fontFamily: 'var(--f-mono)', fontSize: 8.5, fontWeight: 700,
                  color: isPassed ? 'var(--c-stbd)' : isFailed ? 'var(--c-danger)' : isRunning ? 'var(--c-warn)' : 'var(--txt-3)',
                  padding: '2px 5px', borderRadius: 4, background: 'rgba(0,0,0,0.3)',
                  letterSpacing: '0.05em'
                }}>
                  {statusText}
                </span>
              </div>

              <div style={{ fontFamily: 'var(--f-disp)', fontSize: 14, fontWeight: 600, color: textColor }}>
                {SIX_GATE_LABELS[gateId]}
              </div>

              {result && (
                <div style={{ display: 'flex', justifyContent: 'flex-end', marginTop: 8 }}>
                  <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, fontWeight: 600, color: '#fff' }}>
                    耗时 {result.duration_ms} ms
                  </span>
                </div>
              )}
            </div>
          );
        })}
      </div>

      {/* Action status verdict section */}
      <div style={{
        padding: '12px',
        border: verdictBorder,
        borderRadius: 8,
        background: verdictBg,
        textAlign: 'center',
        boxShadow: verdict === 'GO' ? '0 0 12px rgba(0, 227, 179, 0.15)' : 'none',
        transition: 'all 0.3s'
      }}>
        <div style={{ fontFamily: 'var(--f-disp)', fontSize: 13, fontWeight: 700, color: '#fff', letterSpacing: '0.1em', marginBottom: 6 }}>
          决策结论
        </div>
        <span style={{
          display: 'inline-block',
          fontFamily: 'var(--f-disp)',
          fontSize: 14,
          fontWeight: 800,
          color: verdictColor,
          letterSpacing: '0.1em'
        }}>
          {verdictLabel}
        </span>
      </div>
    </div>
  );
}
