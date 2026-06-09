import React, { useState } from 'react';
import { useInjectEncounterMutation, useRemoveEncounterMutation } from '../../api/silApi';

const PRESETS = [
  { rule: 'head_on',          name: '对头 R14',        desc: '正前方 reciprocal 碰撞航线' },
  { rule: 'crossing_giveway', name: '交叉让路 R15-GW', desc: '右舷来船，本船让路' },
  { rule: 'crossing_standon', name: '交叉直航 R15-SO', desc: '左舷来船，本船直航' },
  { rule: 'overtaking',       name: '追越 R13',        desc: '正前慢船，本船追越' },
] as const;

interface Props { inline?: boolean; }

export const EncounterInjectPanel: React.FC<Props> = ({ inline = false }) => {
  const [injectEncounter] = useInjectEncounterMutation();
  const [removeEncounter] = useRemoveEncounterMutation();
  const [activeMmsis, setActiveMmsis] = useState<number[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const onInject = async (rule: string) => {
    setBusy(true); setError(null);
    try {
      const res = await injectEncounter({ rule }).unwrap();
      if (res.accepted) setActiveMmsis((p) => [...p, res.mmsi]);
    } catch (e: any) {
      setError(e?.data?.detail ?? '注入失败（确认仿真已启动）');
    } finally { setBusy(false); }
  };
  const onClear = async () => {
    setBusy(true);
    try {
      await Promise.all(activeMmsis.map((m) => removeEncounter(m).unwrap().catch(() => {})));
      setActiveMmsis([]);
    } finally { setBusy(false); }
  };

  return (
    <div data-testid="encounter-panel"
         style={{ display: 'flex', flexDirection: 'column', gap: 8, width: '100%' }}>
      <div style={{ fontWeight: 600 }}>遭遇注入（COLREGs 预设）</div>
      {PRESETS.map((p) => (
        <button key={p.rule} data-testid={`encounter-${p.rule}`}
                disabled={busy} onClick={() => onInject(p.rule)}
                style={{ textAlign: 'left', padding: '8px 10px', borderRadius: 6,
                         cursor: busy ? 'wait' : 'pointer' }}>
          <div style={{ fontWeight: 600 }}>{p.name}</div>
          <div style={{ fontSize: 12, opacity: 0.7 }}>{p.desc}</div>
        </button>
      ))}
      <button data-testid="encounter-clear" disabled={busy || !activeMmsis.length}
              onClick={onClear} style={{ padding: '6px 10px', borderRadius: 6 }}>
        清除注入船（{activeMmsis.length}）
      </button>
      {error && <div style={{ color: '#dc2626', fontSize: 12 }}>{error}</div>}
    </div>
  );
};
