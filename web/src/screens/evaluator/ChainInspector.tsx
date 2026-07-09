import { useGetDecisionFrameQuery } from '../../api/silApi';

interface ChainInspectorProps {
  evidenceId: string;
  scenarioId: string;
  simT: number;
  onClose: () => void;
}

const formatTime = (simT: number) => {
  const minutes = Math.floor(simT / 60).toString().padStart(2, '0');
  const seconds = Math.floor(simT % 60).toString().padStart(2, '0');
  return `T+${minutes}:${seconds}`;
};

const formatFactValue = (value: unknown) => {
  if (value == null) return 'null';
  if (typeof value === 'object') return JSON.stringify(value);
  return String(value);
};

export function ChainInspector({ evidenceId, scenarioId, simT, onClose }: ChainInspectorProps) {
  const { data, isLoading } = useGetDecisionFrameQuery({ evidenceId, scenarioId, simT });

  return (
    <aside style={{
      position: 'absolute',
      top: 16,
      right: 16,
      bottom: 16,
      width: 420,
      maxWidth: 'calc(100% - 32px)',
      overflow: 'auto',
      zIndex: 20,
      border: '1px solid var(--line-2)',
      background: 'var(--bg-1)',
      color: 'var(--txt-1)',
      padding: 12,
      boxShadow: '0 16px 48px rgba(0, 0, 0, 0.35)',
    }}>
      <header style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 12 }}>
        <div>
          <h2 style={{ margin: 0, fontFamily: 'var(--f-disp)', fontSize: 14 }}>Decision Frame</h2>
          <div style={{ marginTop: 4, fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-3)' }}>
            {formatTime(simT)} / {simT.toFixed(1)} s
          </div>
        </div>
        <button
          type="button"
          onClick={onClose}
          style={{
            border: '1px solid var(--line-2)',
            background: 'transparent',
            color: 'var(--txt-1)',
            padding: '4px 8px',
            cursor: 'pointer',
          }}
        >
          Close
        </button>
      </header>

      {isLoading || !data ? (
        <div style={{ marginTop: 12, fontFamily: 'var(--f-mono)', fontSize: 12 }}>Loading decision frame</div>
      ) : (
        <div style={{ marginTop: 12, display: 'flex', flexDirection: 'column', gap: 12 }}>
          <section>
            {Object.entries(data.chain ?? {}).map(([module, row]) => (
              <div
                key={module}
                style={{
                  borderTop: '1px solid var(--line-1)',
                  padding: '8px 0',
                  fontFamily: 'var(--f-mono)',
                  fontSize: 11,
                }}
              >
                <div style={{ display: 'flex', justifyContent: 'space-between', gap: 8 }}>
                  <strong>{module}</strong>
                  <span>{row.status}</span>
                </div>
                <div style={{ color: 'var(--txt-3)', marginTop: 2 }}>{row.status_source}</div>
                <dl style={{ margin: '6px 0 0', display: 'grid', gridTemplateColumns: 'minmax(96px, auto) minmax(0, 1fr)', gap: '4px 8px' }}>
                  {Object.entries(row.facts ?? {}).map(([key, value]) => (
                    <div key={key} style={{ display: 'contents' }}>
                      <dt style={{ color: 'var(--txt-3)' }}>{key}</dt>
                      <dd style={{ margin: 0, overflowWrap: 'anywhere' }}>{formatFactValue(value)}</dd>
                    </div>
                  ))}
                </dl>
              </div>
            ))}
          </section>

          <section style={{ borderTop: '1px solid var(--line-1)', paddingTop: 8 }}>
            <strong style={{ fontFamily: 'var(--f-disp)', fontSize: 11 }}>Gates</strong>
            {(data.gates ?? []).map((gate) => (
              <div key={`${gate.gate_id}-${gate.source}`} style={{ marginTop: 6, fontFamily: 'var(--f-mono)', fontSize: 11 }}>
                <strong>{gate.gate_id}</strong> {gate.status} {gate.source}
              </div>
            ))}
          </section>

          {(data.nearby_events ?? []).length > 0 && (
            <section style={{ borderTop: '1px solid var(--line-1)', paddingTop: 8 }}>
              <strong style={{ fontFamily: 'var(--f-disp)', fontSize: 11 }}>Nearby Events</strong>
              {data.nearby_events.map((event) => (
                <div key={`${event.event_id ?? event.sim_t}-${event.module}-${event.event_type}`} style={{ marginTop: 6, fontFamily: 'var(--f-mono)', fontSize: 11 }}>
                  {formatTime(event.sim_t)} {event.module} {event.event_type} {event.severity}
                </div>
              ))}
            </section>
          )}
        </div>
      )}
    </aside>
  );
}
