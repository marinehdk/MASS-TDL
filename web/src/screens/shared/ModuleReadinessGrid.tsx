import React, { useState } from 'react';
import { useTelemetryStore } from '../../store';

const MODULE_NAMES: Record<number, string> = {
  1: 'M1 ODD State', 2: 'M2 World Model', 3: 'M3 Mission',
  4: 'M4 Behavior', 5: 'M5 Planner', 6: 'M6 COLREGs',
  7: 'M7 Safety', 8: 'M8 HMI',
};
const HEALTH_COLORS: Record<number, string> = { 1: 'var(--c-stbd)', 2: 'var(--c-warn)', 3: 'var(--c-danger)' };
const HEALTH_LABELS: Record<number, string> = { 1: 'PASS', 2: 'WARN', 3: 'FAIL' };

export const ModuleReadinessGrid: React.FC = () => {
  const pulses = useTelemetryStore((s) => s.modulePulses);
  const [expanded, setExpanded] = useState<number | null>(null);

  const byId: Record<number, any> = {};
  for (const p of pulses) { if (p.moduleId != null) byId[Number(p.moduleId)] = p; }

  return (
    <div data-testid="preflight-modules">
      <div style={{
        fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
        letterSpacing: '0.18em', textTransform: 'uppercase', marginBottom: 6,
      }}>SOFTWARE MODULES · M1–M8</div>
      <div style={{
        display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6,
      }}>
        {[1, 2, 3, 4, 5, 6, 7, 8].map((id) => {
          const p = byId[id];
          const state = p?.state ?? 0;
          const color = HEALTH_COLORS[state] ?? 'var(--txt-3)';
          const isExpanded = expanded === id;
          return (
            <div key={id} style={{
              background: 'var(--bg-1)', border: `1px solid var(--line-1)`,
              borderLeft: `2px solid ${color}`,
            }}>
              <div onClick={() => setExpanded(isExpanded ? null : id)} style={{
                padding: '8px 10px', cursor: 'pointer',
                display: 'flex', alignItems: 'center', gap: 8,
              }}>
                <div style={{
                  width: 10, height: 10, borderRadius: '50%', background: color,
                  boxShadow: state === 1 ? `0 0 6px ${color}` : 'none',
                }} />
                <div style={{ flex: 1 }}>
                  <div style={{
                    fontFamily: 'var(--f-disp)', fontSize: 10.5, color: 'var(--txt-0)',
                    fontWeight: 600, letterSpacing: '0.08em',
                  }}>
                    {MODULE_NAMES[id] || `M${id}`}
                  </div>
                  {p && (
                    <div style={{
                      fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-3)',
                      marginTop: 1,
                    }}>
                      {p.latencyMs ?? '?'}ms · {p.messageDrops ?? 0} drops
                    </div>
                  )}
                </div>
                <span style={{
                  fontFamily: 'var(--f-disp)', fontSize: 9, color,
                  fontWeight: 700, letterSpacing: '0.14em',
                  padding: '1px 6px', border: `1px solid ${color}44`,
                  background: `${color}18`,
                }}>
                  {HEALTH_LABELS[state] ?? '—'}
                </span>
              </div>
              {isExpanded && (
                <div style={{
                  padding: '6px 10px 8px', background: 'var(--bg-0)',
                  borderTop: '1px solid var(--line-1)',
                  fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-2)',
                }}>
                  Module {id} detail — Phase 2: expanded sub-checks
                </div>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
};
