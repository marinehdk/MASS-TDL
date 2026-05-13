import React from 'react';
import { useTelemetryStore } from '../../store';
import type { ASDREvent } from '../../store/telemetryStore';

interface ArpaTargetTableProps {
  expanded: boolean;
  onToggle: () => void;
}

function deriveCpaTcpa(targets: any[], asdrEvents: ASDREvent[]) {
  const cpaMap = new Map<string, { cpa: number; tcpa: number }>();
  for (const e of asdrEvents) {
    if (e.event_type !== 'cpa_update' || !e.payload_json) continue;
    try {
      const p = JSON.parse(e.payload_json);
      if (p.mmsi !== undefined && p.cpa_nm !== undefined) {
        cpaMap.set(String(p.mmsi), { cpa: p.cpa_nm, tcpa: p.tcpa_min ?? 0 });
      }
      if (p.cpa_nm !== undefined && p.mmsi === undefined && targets.length === 1) {
        cpaMap.set('*', { cpa: p.cpa_nm, tcpa: p.tcpa_min ?? 0 });
      }
    } catch { /* noop */ }
  }
  return cpaMap;
}

export const ArpaTargetTable: React.FC<ArpaTargetTableProps> = ({ expanded, onToggle }) => {
  const targets = useTelemetryStore((s) => s.targets);
  const asdrEvents = useTelemetryStore((s) => s.asdrEvents);

  if (!expanded) {
    return (
      <button
        onClick={onToggle}
        data-testid="arpa-table-toggle"
        style={{
          position: 'absolute', top: 54, right: 96, zIndex: 15,
          background: 'var(--bg-1)',
          border: '1px solid var(--line-2)',
          color: 'var(--txt-3)',
          padding: '4px 10px', borderRadius: 3,
          fontFamily: 'var(--f-mono)', fontSize: 10, cursor: 'pointer',
        }}
      >
        ARPA ({targets.length})
      </button>
    );
  }

  const cpaMap = deriveCpaTcpa(targets, asdrEvents);

  return (
    <div
      data-testid="arpa-table"
      style={{
        position: 'absolute', top: 54, right: 96, zIndex: 15,
        background: 'rgba(11,19,32,0.92)',
        border: '1px solid var(--line-3)',
        borderRadius: 6, padding: '6px 8px',
        fontFamily: 'var(--f-mono)', fontSize: 9,
        minWidth: 260, maxHeight: 240, overflowY: 'auto',
      }}
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 4 }}>
        <span style={{ color: 'var(--txt-3)', fontSize: 8, letterSpacing: 1 }}>ARPA TARGETS</span>
        <span onClick={onToggle} style={{ color: 'var(--txt-3)', cursor: 'pointer', fontSize: 10 }}>✕</span>
      </div>
      <table style={{ width: '100%', borderCollapse: 'collapse' }}>
        <thead>
          <tr style={{ color: 'var(--txt-3)', fontSize: 7 }}>
            <th style={{ textAlign: 'left', padding: 1 }}>ID</th>
            <th style={{ textAlign: 'left', padding: 1 }}>BRG°</th>
            <th style={{ textAlign: 'left', padding: 1 }}>RNG</th>
            <th style={{ textAlign: 'left', padding: 1 }}>COG°</th>
            <th style={{ textAlign: 'left', padding: 1 }}>SOG</th>
            <th style={{ textAlign: 'left', padding: 1 }}>CPA</th>
            <th style={{ textAlign: 'left', padding: 1 }}>TCPA</th>
          </tr>
        </thead>
        <tbody>
          {targets.map((t: any, i: number) => {
            const id = t.mmsi ? String(t.mmsi) : `T${i + 1}`;
            const cpaInfo = cpaMap.get(id) ?? cpaMap.get('*');
            const cpaVal = cpaInfo?.cpa;
            const cpaColor = cpaVal != null
              ? cpaVal < 1.0 ? 'var(--c-danger)' : cpaVal < 2.0 ? 'var(--c-warn)' : 'var(--txt-1)'
              : 'var(--txt-3)';
            return (
              <tr key={id} style={{ borderTop: '1px solid var(--line-1)' }}>
                <td style={{ padding: 1, color: 'var(--c-info)' }}>{id}</td>
                <td style={{ padding: 1 }}>—</td>
                <td style={{ padding: 1 }}>—</td>
                <td style={{ padding: 1 }}>{t.kinematics?.cog != null ? ((t.kinematics.cog * 180 / Math.PI + 360) % 360).toFixed(0) : '—'}°</td>
                <td style={{ padding: 1 }}>{t.kinematics?.sog != null ? (t.kinematics.sog * 1.944).toFixed(1) : '—'}</td>
                <td style={{ padding: 1, color: cpaColor }}>{cpaInfo?.cpa?.toFixed(2) ?? '—'}</td>
                <td style={{ padding: 1 }}>{cpaInfo?.tcpa?.toFixed(1) ?? '—'}m</td>
              </tr>
            );
          })}
          {targets.length === 0 && (
            <tr><td colSpan={7} style={{ color: 'var(--txt-3)', padding: 4, textAlign: 'center' }}>No targets</td></tr>
          )}
        </tbody>
      </table>
    </div>
  );
};
