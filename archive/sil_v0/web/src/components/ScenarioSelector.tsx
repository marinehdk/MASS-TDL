import { useEffect, useState, memo } from 'react';

interface Props {
  value: string;
  onChange: (id: string) => void;
}

export const ScenarioSelector = memo(function ScenarioSelector({ value, onChange }: Props) {
  const [scenarios, setScenarios] = useState<string[]>([]);

  useEffect(() => {
    fetch('/sil/scenario/list')
      .then(r => r.json())
      .then(setScenarios)
      .catch(() => setScenarios([]));
  }, []);

  if (scenarios.length === 0) return null;

  return (
    <div style={{
      padding: '10px 14px',
      borderBottom: '1px solid var(--line-1)',
    }}>
      <label style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '9px',
        color: 'var(--text-2)',
        textTransform: 'uppercase',
        letterSpacing: '1px',
        display: 'block',
        marginBottom: '6px',
      }}>
        Scenario
      </label>
      <select
        value={value}
        onChange={e => onChange(e.target.value)}
        style={{
          width: '100%',
          background: 'var(--bg-2)',
          color: 'var(--text-1)',
          border: '1px solid var(--line-1)',
          fontFamily: 'var(--fnt-mono)',
          fontSize: '11px',
          padding: '6px 8px',
          borderRadius: 'var(--radius-none)',
          outline: 'none',
        }}
      >
        {scenarios.map(s => (
          <option key={s} value={s}>{s.replace('.yaml', '')}</option>
        ))}
      </select>
    </div>
  );
});
