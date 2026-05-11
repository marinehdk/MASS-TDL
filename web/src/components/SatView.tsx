import { useState, memo, type ReactNode } from 'react';

interface Props {
  sat1Content: ReactNode;
}

type SatTab = 1 | 2 | 3;

export const SatView = memo(function SatView({ sat1Content }: Props) {
  const [tab, setTab] = useState<SatTab>(1);

  const tabStyle = (t: SatTab) => ({
    flex: 1,
    padding: '6px 0',
    textAlign: 'center' as const,
    fontFamily: 'var(--fnt-mono)',
    fontSize: '9px',
    textTransform: 'uppercase' as const,
    letterSpacing: '1px',
    color: tab === t ? 'var(--text-1)' : 'var(--text-2)',
    background: tab === t ? 'var(--bg-2)' : 'transparent',
    border: 'none',
    borderBottom: tab === t ? '2px solid var(--color-stbd)' : '2px solid transparent',
    cursor: 'pointer',
  });

  const placeholder = (label: string, phase: string) => (
    <div style={{
      padding: '20px 14px',
      fontFamily: 'var(--fnt-mono)',
      fontSize: '10px',
      color: 'var(--text-3)',
      textAlign: 'center',
      borderBottom: '1px solid var(--line-1)',
    }}>{label} — {phase}</div>
  );

  return (
    <div>
      <div style={{
        display: 'flex',
        borderBottom: '1px solid var(--line-1)',
      }}>
        <button style={tabStyle(1)} onClick={() => setTab(1)}>SAT-1</button>
        <button style={tabStyle(2)} onClick={() => setTab(2)}>SAT-2</button>
        <button style={tabStyle(3)} onClick={() => setTab(3)}>SAT-3</button>
      </div>
      <div style={{ display: tab === 1 ? 'block' : 'none' }}>{sat1Content}</div>
      {tab === 2 && placeholder('Comprehension subsystem', 'Phase 2 增项')}
      {tab === 3 && placeholder('Projection subsystem', 'D3.4')}
    </div>
  );
});
