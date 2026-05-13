import React, { useEffect, useRef, useState } from 'react';
import { useTelemetryStore } from '../../store';

export const LiveLogStream: React.FC = () => {
  const logEntries = useTelemetryStore((s) => (s as any).preflightLog ?? []);
  const [paused, setPaused] = useState(false);
  const [filter, setFilter] = useState('');
  const endRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!paused) endRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [logEntries, paused]);

  const filtered = filter
    ? logEntries.filter((e: any) => {
        const txt = (e.message ?? '').toLowerCase();
        return txt.includes(filter.toLowerCase());
      })
    : logEntries;

  const colorForLevel = (level: string) => {
    if (level === 'error') return 'var(--c-danger)';
    if (level === 'warn') return 'var(--c-warn)';
    return 'var(--txt-2)';
  };

  return (
    <div data-testid="preflight-livelog" style={{
      height: '100%', display: 'flex', flexDirection: 'column',
      background: 'var(--bg-1)', border: '1px solid var(--line-1)',
    }}>
      {/* Header */}
      <div style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        padding: '4px 8px', borderBottom: '1px solid var(--line-1)',
      }}>
        <span style={{
          fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
          letterSpacing: '0.18em', textTransform: 'uppercase',
        }}>LIVE LOG</span>
        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <input placeholder="Filter..." value={filter}
            onChange={(e) => setFilter(e.target.value)}
            style={{
              width: 100, background: 'var(--bg-0)', border: '1px solid var(--line-2)',
              color: 'var(--txt-1)', padding: '2px 6px', fontSize: 9,
              fontFamily: 'var(--f-mono)', borderRadius: 2,
            }}
          />
          <button onClick={() => setPaused(!paused)} style={{
            background: paused ? 'rgba(240,183,47,0.15)' : 'transparent',
            border: `1px solid ${paused ? 'var(--c-warn)' : 'var(--line-2)'}`,
            color: paused ? 'var(--c-warn)' : 'var(--txt-3)',
            padding: '1px 8px', cursor: 'pointer', fontSize: 9,
            fontFamily: 'var(--f-mono)', borderRadius: 2,
          }}>
            {paused ? 'PAUSED' : 'LIVE'}
          </button>
        </div>
      </div>
      {/* Log entries */}
      <div style={{
        flex: 1, overflowY: 'auto', padding: '4px 8px',
        fontFamily: 'var(--f-mono)', fontSize: 9, lineHeight: 1.5,
      }}>
        {filtered.length === 0 ? (
          <div style={{ color: 'var(--txt-3)', padding: 8 }}>
            {logEntries.length === 0 ? 'No log entries yet...' : 'No matching entries'}
          </div>
        ) : (
          filtered.map((entry: any, i: number) => {
            const color = colorForLevel(entry.level ?? 'info');
            return (
              <div key={i} style={{ color }}>
                <span style={{ color: 'var(--txt-3)', marginRight: 8 }}>
                  [{entry.timestamp ?? '--:--:--'}]
                </span>
                {entry.message ?? JSON.stringify(entry)}
              </div>
            );
          })
        )}
        <div ref={endRef} />
      </div>
    </div>
  );
};
