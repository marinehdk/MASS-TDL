import React, { useState } from 'react';
import { useClearEncountersMutation, useInjectEncounterMutation } from '../../api/silApi';

const PRESETS = [
  { rule: 'head_on',          name: '对头 R14',        desc: '正前方 reciprocal 碰撞航线' },
  { rule: 'crossing_giveway', name: '交叉让路 R15-GW', desc: '右舷来船，本船让路' },
  { rule: 'crossing_standon', name: '交叉直航 R15-SO', desc: '左舷来船，本船直航' },
  { rule: 'overtaking',       name: '追越 R13',        desc: '正前慢船，本船追越' },
] as const;

interface Props { inline?: boolean; }

export const EncounterInjectPanel: React.FC<Props> = ({ inline = false }) => {
  const [injectEncounter] = useInjectEncounterMutation();
  const [clearEncounters] = useClearEncountersMutation();
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
  const onInjectRandomDemo = async () => {
    setBusy(true); setError(null);
    const acceptedMmsis: number[] = [];
    try {
      const shuffled = [...PRESETS].sort(() => Math.random() - 0.5).slice(0, 3);
      for (const preset of shuffled) {
        const response = await injectEncounter({ rule: preset.rule }).unwrap();
        if (response.accepted) acceptedMmsis.push(response.mmsi);
      }
      if (acceptedMmsis.length > 0) setActiveMmsis((p) => [...p, ...acceptedMmsis]);
    } catch (e: any) {
      if (acceptedMmsis.length > 0) setActiveMmsis((p) => [...p, ...acceptedMmsis]);
      setError(e?.data?.detail ?? '批量注入失败（确认仿真已启动）');
    } finally { setBusy(false); }
  };
  const onClear = async () => {
    setBusy(true); setError(null);
    try {
      const res = await clearEncounters().unwrap();
      setActiveMmsis(res.failed_mmsis ?? []);
    } catch (e: any) {
      setError(e?.data?.detail ?? '清除失败');
    } finally { setBusy(false); }
  };

  return (
    <div data-testid="encounter-panel"
         style={{ display: 'flex', flexDirection: 'column', gap: 8, width: '100%' }}>
      <div style={{ fontWeight: 600 }}>遭遇注入（COLREGs 预设）</div>
      <button data-testid="encounter-random-demo"
              disabled={busy} onClick={onInjectRandomDemo}
              style={{ textAlign: 'left', padding: '8px 10px', borderRadius: 6,
                       cursor: busy ? 'wait' : 'pointer',
                       border: '1px solid rgba(91,192,190,0.55)' }}>
        <div style={{ fontWeight: 700 }}>随机三船避碰演示</div>
        <div style={{ fontSize: 12, opacity: 0.7 }}>从预设中抽取 3 个目标船连续注入</div>
      </button>
      {PRESETS.map((p) => (
        <button key={p.rule} data-testid={`encounter-${p.rule}`}
                disabled={busy} onClick={() => onInject(p.rule)}
                style={{ textAlign: 'left', padding: '8px 10px', borderRadius: 6,
                         cursor: busy ? 'wait' : 'pointer' }}>
          <div style={{ fontWeight: 600 }}>{p.name}</div>
          <div style={{ fontSize: 12, opacity: 0.7 }}>{p.desc}</div>
        </button>
      ))}
      <button data-testid="encounter-clear" disabled={busy}
              onClick={onClear} style={{ padding: '6px 10px', borderRadius: 6 }}>
        清除注入船（{activeMmsis.length}）
      </button>
      {error && <div style={{ color: '#dc2626', fontSize: 12 }}>{error}</div>}
    </div>
  );
};
