import React from 'react';
import { useTelemetryStore } from '../../store';

interface ModuleDrilldownProps {
  visible: boolean;
  onClose: () => void;
}

const MODULE_NAMES = ['M1 ODD', 'M2 World', 'M3 Mission', 'M4 Behavior', 'M5 Planner', 'M6 COLREGs', 'M7 Safety', 'M8 HMI'];
const HEALTH_LABELS: Record<number, string> = { 1: 'GREEN', 2: 'AMBER', 3: 'RED' };
const HEALTH_COLORS: Record<number, string> = { 1: 'var(--c-stbd)', 2: 'var(--c-warn)', 3: 'var(--c-danger)' };

export const ModuleDrilldown: React.FC<ModuleDrilldownProps> = ({ visible, onClose }) => {
  const pulses = useTelemetryStore((s) => s.modulePulses);

  if (!visible) return null;

  const byId: Record<number, any> = {};
  for (const p of pulses) { if (p.moduleId != null) byId[Number(p.moduleId)] = p; }

  return (
    <div
      data-testid="module-drilldown"
      style={{
        position: 'absolute', bottom: 58, left: 16, zIndex: 30,
        background: 'rgba(11,19,32,0.96)',
        border: '1px solid var(--line-3)',
        borderRadius: 8, padding: '8px 10px',
        fontFamily: 'var(--f-mono)', fontSize: 9,
        minWidth: 340,
      }}
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 6 }}>
        <span style={{ color: 'var(--txt-3)', fontSize: 8, letterSpacing: 1 }}>MODULE HEALTH</span>
        <span onClick={onClose} style={{ color: 'var(--txt-3)', cursor: 'pointer', fontSize: 10 }}>✕</span>
      </div>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4 }}>
        {MODULE_NAMES.map((name, i) => {
          const p = byId[i + 1];
          const color = p ? HEALTH_COLORS[p.state ?? 0] ?? 'var(--txt-3)' : '#333';
          return (
            <div key={name} style={{
              flex: '1 1 45%', minWidth: 140,
              background: 'var(--bg-1)',
              border: `1px solid ${color}22`,
              borderRadius: 4, padding: 4,
            }}>
              <span style={{ color, fontSize: 8 }}>● {name}</span>
              <div style={{ color: 'var(--txt-3)', fontSize: 7, marginTop: 2 }}>
                {p ? (
                  <>state: {HEALTH_LABELS[p.state ?? 0] ?? '?'} · lat: {p.latencyMs ?? '?'}ms · drops: {p.messageDrops ?? 0}</>
                ) : (
                  'no data'
                )}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};
