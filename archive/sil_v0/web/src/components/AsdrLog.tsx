import { memo, useEffect, useRef } from 'react';
import type { AsdrEvent } from '../types/sil';

interface Props {
  events: AsdrEvent[];
}

export const AsdrLog = memo(function AsdrLog({ events }: Props) {
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [events.length]);

  return (
    <div style={{
      padding: '10px 14px',
      flex: 1,
      display: 'flex',
      flexDirection: 'column',
    }}>
      <div style={{
        fontFamily: 'var(--fnt-disp)',
        fontSize: '10px',
        letterSpacing: '0.22em',
        color: 'var(--txt-3)',
        textTransform: 'uppercase',
        marginBottom: '6px',
      }}>ASDR Certification Evidence Log</div>
      <div style={{
        height: '100px',
        overflowY: 'auto',
        fontFamily: 'var(--fnt-mono)',
        fontSize: '10px',
        lineHeight: 1.6,
        color: 'var(--txt-2)',
        background: 'var(--bg-2)',
        border: '1px solid var(--line-1)',
        borderRadius: 'var(--radius-none)',
        padding: '6px 8px',
        flex: 1,
      }}>
        {events.length === 0 ? (
          <div style={{ color: 'var(--txt-3)' }}>
            {'—'} Click Run Scenario to start simulation {'—'}
          </div>
        ) : (
          events.map((ev, i) => (
            <div key={i} style={{
              padding: '2px 0',
              borderBottom: '0.5px solid var(--line-1)',
              whiteSpace: 'nowrap',
              overflow: 'hidden',
              textOverflow: 'ellipsis',
            }}>
              <span style={{ color: 'var(--txt-3)' }}>{ev.timestamp}</span>
              {' [ASDR] '}step={ev.step} rule={ev.rule}{' '}
              cpa={ev.cpa_nm.toFixed(2)}nm tcpa={ev.tcpa_s}s{' '}
              act=&quot;{ev.action}&quot;{' '}
              <span style={{
                color: ev.verdict === 'RISK' ? 'var(--color-port)' : 'var(--color-stbd)',
                fontWeight: ev.verdict === 'RISK' ? 600 : 400,
              }}>
                {ev.verdict}
              </span>
            </div>
          ))
        )}
        <div ref={bottomRef} />
      </div>
    </div>
  );
});
