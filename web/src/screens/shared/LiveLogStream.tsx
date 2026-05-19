import React, { useEffect, useMemo, useRef, useState } from 'react';
import { useTelemetryStore } from '../../store';

interface LiveLogStreamProps {
  nodeFilter?: string;
  maxLines?: number;
}

export const LiveLogStream: React.FC<LiveLogStreamProps> = ({ nodeFilter, maxLines = 200 }) => {
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

  const displayLines = useMemo(() => {
    let lines = filtered;
    if (nodeFilter) {
      const lower = nodeFilter.toLowerCase();
      lines = lines.filter((e: any) => {
        const txt = (e.message ?? '').toLowerCase();
        return lower.split('|').some(term => txt.includes(term));
      });
    }
    if (maxLines && lines.length > maxLines) lines = lines.slice(-maxLines);
    return lines;
  }, [filtered, nodeFilter, maxLines]);

  const colorForLevel = (level: string) => {
    if (level === 'error') return 'var(--c-danger)';
    if (level === 'warn') return 'var(--c-warn)';
    return 'var(--txt-1)';
  };

  return (
    <div data-testid="preflight-livelog" style={{
      height: '100%', display: 'flex', flexDirection: 'column',
      background: 'var(--bg-1)', border: 'none',
    }}>
      {/* Log entries */}
      <div style={{
        flex: 1, overflowY: 'auto', padding: '10px 12px',
        background: 'rgba(0,0,0,0.15)'
      }}>
        {displayLines.length === 0 ? (
          <div style={{ color: 'var(--txt-3)', padding: 8, fontFamily: 'var(--f-mono)', fontSize: 12 }}>
            {logEntries.length === 0 ? '暂无自检日志流输出...' : '未找到匹配的日志条目'}
          </div>
        ) : (
          displayLines.map((entry: any, i: number) => {
            const color = colorForLevel(entry.level ?? 'info');
            return (
              <div key={i} style={{ color, fontFamily: 'var(--f-mono)', fontSize: 12, lineHeight: 1.6, marginBottom: 4, wordBreak: 'break-all' }}>
                <span style={{ color: 'var(--c-phos)', marginRight: 10, fontWeight: 600 }}>
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
