import type { RuntimeMode } from '../../api/silApi';

export function RuntimeModeSwitch({ mode, onChange }: { mode: RuntimeMode; onChange: (mode: RuntimeMode) => void }) {
  return (
    <div style={{ display: 'inline-flex', border: '1px solid var(--line-1)', borderRadius: 6, overflow: 'hidden' }}>
      {(['internal', 'integration'] as RuntimeMode[]).map((item) => (
        <button
          key={item}
          type="button"
          onClick={() => onChange(item)}
          style={{
            height: 36,
            minWidth: 86,
            border: 0,
            borderRadius: 0,
            background: mode === item ? 'var(--c-info)' : 'var(--bg-2)',
            color: mode === item ? 'var(--bg-0)' : 'var(--txt-1)',
            fontWeight: 800,
          }}
        >
          {item === 'internal' ? '内测' : '集成'}
        </button>
      ))}
    </div>
  );
}
