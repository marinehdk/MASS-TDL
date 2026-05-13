import React, { useState } from 'react';
import { useUIStore } from '../../store';
import { useInjectFaultMutation, useCancelFaultMutation } from '../../api/silApi';

const FAULT_CATALOG = [
  { id: 'ais_dropout',  m: 'M2', sev: 'warn', name: 'AIS Dropout (T01)',     dur: 30 },
  { id: 'radar_noise',  m: 'M2', sev: 'warn', name: 'Radar Rain Clutter',    dur: 40 },
  { id: 'roc_link_loss',m: 'M8', sev: 'crit', name: 'ROC Link Loss',         dur: 10 },
];

export const FaultInjectPanel: React.FC = () => {
  const viewMode = useUIStore((s) => s.viewMode);
  const [injectFault] = useInjectFaultMutation();
  const [cancelFault] = useCancelFaultMutation();
  const [activeFaults, setActiveFaults] = useState<Set<string>>(new Set());
  const [error, setError] = useState<string | null>(null);
  const [duration, setDuration] = useState(30);

  if (viewMode !== 'god') return null;

  const handleInject = async (fault: typeof FAULT_CATALOG[0]) => {
    try {
      const result = await injectFault({
        type: fault.id as any,
        duration_s: duration,
      }).unwrap();
      if (result.accepted) {
        setActiveFaults((prev) => new Set(prev).add(fault.id));
        setError(null);
      }
    } catch (e: any) {
      if (e?.status === 409) {
        setError(`Fault already active: ${fault.name}`);
      } else {
        setError(e?.data?.detail ?? 'Injection failed');
      }
    }
  };

  const sevColor = (sev: string) => sev === 'crit' ? 'var(--c-danger)' : 'var(--c-warn)';

  return (
    <div data-testid="fault-panel" style={{
      position: 'absolute', right: 16, bottom: 120, width: 320, zIndex: 20,
      background: 'rgba(11,19,32,0.95)',
      border: '1px solid var(--c-danger)',
      borderLeft: '3px solid var(--c-danger)',
      padding: 12,
    }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 8 }}>
        <span style={{
          fontFamily: 'var(--f-disp)', fontSize: 10.5, color: 'var(--c-danger)',
          letterSpacing: '0.18em', fontWeight: 700, textTransform: 'uppercase',
        }}>FAULT INJECTION</span>
      </div>

      {/* Duration slider */}
      <div style={{ marginBottom: 8 }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 2 }}>
          <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>Duration</span>
          <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-1)' }}>{duration}s</span>
        </div>
        <input type="range" min={5} max={120} value={duration}
          onChange={(e) => setDuration(Number(e.target.value))}
          style={{ width: '100%', accentColor: 'var(--c-danger)' }}
        />
      </div>

      {/* Fault cards */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
        {FAULT_CATALOG.map((f) => {
          const isActive = activeFaults.has(f.id);
          const c = sevColor(f.sev);
          return (
            <div key={f.id}
              data-testid={`fault-inject-${f.id.split('_')[0]}`}
              onClick={() => !isActive && handleInject(f)}
              style={{
                display: 'grid', gridTemplateColumns: '30px 40px 1fr 40px', gap: 6,
                alignItems: 'center', padding: '6px 10px', cursor: isActive ? 'default' : 'pointer',
                background: isActive ? `${c}22` : 'var(--bg-0)',
                border: `1px solid ${isActive ? c : 'var(--line-1)'}`,
                borderLeft: `2px solid ${c}`,
              }}
            >
              <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9.5, color: c, fontWeight: 700 }}>{f.m}</span>
              <span style={{
                fontFamily: 'var(--f-disp)', fontSize: 8.5, color: c, fontWeight: 700,
                letterSpacing: '0.12em', textTransform: 'uppercase',
              }}>{f.sev}</span>
              <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9.5, color: 'var(--txt-1)' }}>{f.name}</span>
              <span style={{ fontFamily: 'var(--f-mono)', fontSize: 8.5, color: isActive ? c : 'var(--txt-3)' }}>
                {isActive ? 'ON' : `${f.dur}s`}
              </span>
            </div>
          );
        })}
      </div>

      {error && (
        <div style={{
          marginTop: 6, padding: '4px 8px', background: 'rgba(248,81,73,0.12)',
          border: '1px solid var(--c-danger)',
          fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--c-danger)',
        }}>
          {error}
        </div>
      )}

      <div style={{
        fontFamily: 'var(--f-mono)', fontSize: 8.5, color: 'var(--txt-3)', marginTop: 6,
      }}>
        All faults written to ASDR
      </div>
    </div>
  );
};
