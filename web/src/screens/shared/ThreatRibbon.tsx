import React from 'react';
import { useTelemetryStore } from '../../store';

interface ThreatRibbonProps {
  viewMode: 'captain' | 'god';
}

export const ThreatRibbon: React.FC<ThreatRibbonProps> = ({ viewMode }) => {
  const targets = useTelemetryStore((s) => s.targets);
  const asdrEvents = useTelemetryStore((s) => s.asdrEvents);

  // Derive CPA/TCPA from ASDR events
  const cpaMap = new Map<string, { cpa: number; tcpa: number }>();
  for (const e of asdrEvents) {
    if (e.event_type === 'cpa_update' && e.payload_json) {
      try {
        const p = JSON.parse(e.payload_json);
        if (p.mmsi && p.cpa_nm !== undefined) {
          cpaMap.set(String(p.mmsi), { cpa: p.cpa_nm, tcpa: p.tcpa_min ?? 0 });
        }
      } catch { /* noop */ }
    }
  }

  // Sort by CPA ascending (most dangerous first)
  const sorted = [...targets]
    .map((t: any) => {
      const id = String(t.mmsi ?? '');
      const cpa = cpaMap.get(id);
      return { ...t, _cpa: cpa?.cpa, _tcpa: cpa?.tcpa };
    })
    .sort((a: any, b: any) => (a._cpa ?? 999) - (b._cpa ?? 999));

  const maxItems = viewMode === 'captain' ? 5 : 10;
  const visible = sorted.slice(0, maxItems);
  const overflow = sorted.length - maxItems;

  if (sorted.length === 0) return null;

  return (
    <div data-testid="threat-ribbon" style={{
      position: 'absolute', top: 54, left: 0, right: 0, zIndex: 14,
      display: 'flex', flexDirection: 'column', gap: 4,
      padding: '6px 12px',
      background: 'rgba(11,19,32,0.90)', backdropFilter: 'blur(4px)',
      borderBottom: '1px solid var(--line-1)',
    }}>
      {/* Captain: 1 row chips only. God: 2 rows */}
      <div style={{ display: 'flex', gap: 6, alignItems: 'center', flexWrap: 'wrap' }}>
        {visible.map((t: any, i: number) => {
          const cpa = t._cpa;
          const color = cpa != null
            ? cpa < 1.0 ? 'var(--c-danger)' : cpa < 2.0 ? 'var(--c-warn)' : 'var(--c-stbd)'
            : 'var(--txt-3)';
          return (
            <div key={i} data-testid={`threat-chip-${t.mmsi ?? i}`} style={{
              display: 'flex', alignItems: 'center', gap: 4,
              padding: '2px 8px', border: `1px solid ${color}44`,
              borderLeft: `2px solid ${color}`, background: `${color}11`,
              fontFamily: 'var(--f-mono)', fontSize: 9,
              cursor: 'pointer',
            }}>
              <span style={{ color }}>●</span>
              <span style={{ color: 'var(--txt-1)', fontWeight: 600 }}>
                {t.mmsi ? `T${String(t.mmsi).slice(-2)}` : `T${i + 1}`}
              </span>
              {viewMode === 'god' && (
                <span style={{ color: 'var(--txt-3)', fontSize: 8 }}>
                  {t._role ?? '—'}
                </span>
              )}
              <span style={{ color }}>{cpa?.toFixed(2) ?? '—'}nm</span>
              {viewMode === 'god' && (
                <span style={{ color: 'var(--txt-3)', fontSize: 8 }}>
                  {t._tcpa?.toFixed(1) ?? '—'}min
                </span>
              )}
            </div>
          );
        })}
        {overflow > 0 && (
          <span style={{
            fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-3)',
            padding: '2px 6px', border: '1px solid var(--line-2)',
          }}>
            +{overflow} more
          </span>
        )}
      </div>
    </div>
  );
};
