import React, { useState, useEffect, useRef } from 'react';

interface AsdrEvent {
  time: string;
  type: string;
  module: string;
  payload: any;
  hash: string;
}

interface AsdrLedgerProps {
  events: AsdrEvent[];
  onEventSelect?: (timeSec: number) => void;
  currentTimeSec?: number;
}

type FilterLevel = 'ALL' | 'INFO' | 'WARN' | 'CRIT';

const FILTERS: FilterLevel[] = ['ALL', 'INFO', 'WARN', 'CRIT'];

const SEV_COLORS: Record<string, string> = {
  INFO: 'var(--c-info)', WARN: 'var(--c-warn)', CRIT: 'var(--c-danger)',
};

const timeToSeconds = (timeStr: string): number => {
  const match = timeStr.match(/T\+(\d+):(\d+)/);
  if (match) {
    const mins = parseInt(match[1], 10);
    const secs = parseInt(match[2], 10);
    return mins * 60 + secs;
  }
  return 0;
};

export const AsdrLedger: React.FC<AsdrLedgerProps> = ({ events, onEventSelect, currentTimeSec }) => {
  const [filter, setFilter] = useState<FilterLevel>('ALL');
  const [page, setPage] = useState(0);
  const PAGE_SIZE = 50;

  const filtered = filter === 'ALL' ? events : events.filter((e) => e.type.includes(filter));
  const paged = filtered.slice(page * PAGE_SIZE, (page + 1) * PAGE_SIZE);
  const totalPages = Math.ceil(filtered.length / PAGE_SIZE);

  // Find index of the event closest to currentTimeSec in the filtered list
  let closestIndex = -1;
  if (currentTimeSec !== undefined && filtered.length > 0) {
    let minDiff = Infinity;
    for (let i = 0; i < filtered.length; i++) {
      const diff = Math.abs(timeToSeconds(filtered[i].time) - currentTimeSec);
      if (diff < minDiff) {
        minDiff = diff;
        closestIndex = i;
      }
    }
  }

  const lastTargetPageRef = useRef<number>(-1);
  const activeRowRef = useRef<HTMLTableRowElement | null>(null);

  // Sync pagination page with the closest event
  useEffect(() => {
    if (closestIndex !== -1) {
      const targetPage = Math.floor(closestIndex / PAGE_SIZE);
      if (targetPage !== lastTargetPageRef.current && targetPage >= 0 && targetPage < totalPages) {
        lastTargetPageRef.current = targetPage;
        setPage(targetPage);
      }
    }
  }, [closestIndex, totalPages]);

  // Reset lastTargetPageRef when page changes manually (so if time shifts back/forth to this page later, it still triggers sync)
  useEffect(() => {
    if (closestIndex !== -1) {
      const targetPage = Math.floor(closestIndex / PAGE_SIZE);
      if (page === targetPage) {
        lastTargetPageRef.current = targetPage;
      }
    }
  }, [page, closestIndex]);

  // Scroll active row into view
  useEffect(() => {
    if (activeRowRef.current && typeof activeRowRef.current.scrollIntoView === 'function') {
      activeRowRef.current.scrollIntoView({
        behavior: 'auto',
        block: 'nearest',
      });
    }
  }, [closestIndex, page]);

  return (
    <div data-testid="asdr-ledger" style={{
      display: 'flex', flexDirection: 'column', height: '100%',
      background: 'var(--bg-1)', border: '1px solid var(--line-1)',
    }}>
      {/* Header + filters */}
      <div style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        padding: '6px 8px', borderBottom: '1px solid var(--line-1)',
      }}>
        <span style={{
          fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
          letterSpacing: '0.16em', textTransform: 'uppercase',
        }}>
          ASDR LEDGER ({filtered.length})
        </span>
        <div style={{ display: 'flex', gap: 2 }}>
          {FILTERS.map((f) => (
            <button key={f} onClick={() => { setFilter(f); setPage(0); }}
              data-testid={`asdr-filter-${f.toLowerCase()}`}
              style={{
                background: filter === f ? 'var(--c-phos)' : 'transparent',
                border: `1px solid ${filter === f ? 'var(--c-phos)' : 'var(--line-2)'}`,
                color: filter === f ? 'var(--bg-0)' : 'var(--txt-3)',
                padding: '2px 8px', cursor: 'pointer', fontSize: 8.5,
                fontFamily: 'var(--f-disp)', letterSpacing: '0.10em', fontWeight: 600,
              }}>
              {f}
            </button>
          ))}
        </div>
      </div>

      {/* Table */}
      <div style={{ flex: 1, overflowY: 'auto', fontSize: 8.5 }}>
        <table style={{
          width: '100%', borderCollapse: 'collapse',
          fontFamily: 'var(--f-mono)',
        }}>
          <thead>
            <tr style={{
              color: 'var(--txt-3)', fontSize: 7.5,
              position: 'sticky', top: 0, background: 'var(--bg-1)',
            }}>
              <th style={{ textAlign: 'left', padding: '2px 4px', width: 28 }}>#</th>
              <th style={{ textAlign: 'left', padding: '2px 4px', width: 52 }}>TIME</th>
              <th style={{ textAlign: 'left', padding: '2px 4px', width: 80 }}>TYPE</th>
              <th style={{ textAlign: 'left', padding: '2px 4px', width: 28 }}>MOD</th>
              <th style={{ textAlign: 'left', padding: '2px 4px' }}>PAYLOAD</th>
              <th style={{ textAlign: 'left', padding: '2px 4px', width: 72 }}>SHA-256</th>
            </tr>
          </thead>
          <tbody>
            {paged.map((e, i) => {
              const sevColor = SEV_COLORS[e.type.split('_')[0] === 'CRIT' ? 'CRIT' :
                e.type.includes('WARN') ? 'WARN' : 'INFO'] ?? 'var(--txt-2)';
              const eventIndex = page * PAGE_SIZE + i;
              const isActive = closestIndex === eventIndex;
              return (
                <tr
                  key={i}
                  ref={isActive ? activeRowRef : undefined}
                  onClick={() => onEventSelect?.(timeToSeconds(e.time))}
                  style={{
                    borderTop: '1px solid var(--line-1)',
                    color: 'var(--txt-2)',
                    cursor: onEventSelect ? 'pointer' : 'default',
                    background: isActive ? 'rgba(91,192,190,0.15)' : undefined,
                  }}
                  className={isActive ? 'highlighted' : undefined}
                  data-testid={isActive ? 'active-row' : undefined}
                >
                  <td style={{ padding: '2px 4px', color: 'var(--txt-3)' }}>
                    {page * PAGE_SIZE + i + 1}
                  </td>
                  <td style={{ padding: '2px 4px' }}>{e.time}</td>
                  <td style={{ padding: '2px 4px', color: sevColor }}>{e.type}</td>
                  <td style={{ padding: '2px 4px' }}>{e.module}</td>
                  <td style={{
                    padding: '2px 4px', maxWidth: 200,
                    overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap',
                  }}>
                    {typeof e.payload === 'object' ? JSON.stringify(e.payload) : e.payload}
                  </td>
                  <td style={{
                    padding: '2px 4px', color: 'var(--txt-3)', fontSize: 7.5,
                  }}>
                    {e.hash ? e.hash.slice(0, 8) : ''}
                  </td>
                </tr>
              );
            })}
            {paged.length === 0 && (
              <tr>
                <td colSpan={6} style={{ padding: 12, textAlign: 'center', color: 'var(--txt-3)' }}>
                  No events
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>

      {/* Pagination + SHA chain section */}
      <div style={{
        padding: '4px 8px', borderTop: '1px solid var(--line-1)',
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-3)',
      }}>
        <div style={{ display: 'flex', gap: 4 }}>
          <button onClick={() => setPage(Math.max(0, page - 1))} disabled={page === 0} style={{
            background: 'transparent', border: '1px solid var(--line-2)', color: 'var(--txt-2)',
            padding: '1px 6px', cursor: 'pointer', fontSize: 9,
          }}>&#9664;</button>
          <span>{page + 1}/{Math.max(1, totalPages)}</span>
          <button onClick={() => setPage(Math.min(totalPages - 1, page + 1))} disabled={page >= totalPages - 1} style={{
            background: 'transparent', border: '1px solid var(--line-2)', color: 'var(--txt-2)',
            padding: '1px 6px', cursor: 'pointer', fontSize: 9,
          }}>&#9654;</button>
        </div>

        <div style={{ display: 'flex', gap: 6 }}>
          <span>SHA-256: {events.length > 0 && events[events.length - 1].hash ? events[events.length - 1].hash.slice(0, 12) + '...' : '0xDEADBEEF'}</span>
          <button style={{
            background: 'transparent', border: '1px solid var(--c-stbd)',
            color: 'var(--c-stbd)', padding: '1px 8px', cursor: 'pointer',
            fontFamily: 'var(--f-disp)', fontSize: 8, letterSpacing: '0.10em',
          }}>VERIFY</button>
        </div>
      </div>
    </div>
  );
};
