type RuntimeActionLogEntry = {
  time: string;
  message: string;
  level?: 'info' | 'warn' | 'error';
};

const LEVEL_COLOR: Record<NonNullable<RuntimeActionLogEntry['level']>, string> = {
  info: 'var(--c-info)',
  warn: 'var(--c-warn)',
  error: 'var(--c-danger)',
};

export function RuntimeActionLog({ entries }: { entries: RuntimeActionLogEntry[] }) {
  return (
    <section style={{ display: 'grid', gap: 10 }}>
      <h3 style={{ margin: 0, color: 'var(--txt-0)', fontSize: 14 }}>Runtime Actions</h3>
      <ol style={{ display: 'grid', gap: 8, margin: 0, padding: 0, listStyle: 'none' }}>
        {entries.map((entry, index) => {
          const level = entry.level ?? 'info';
          return (
            <li
              key={`${entry.time}-${entry.message}-${index}`}
              style={{
                border: '1px solid var(--line-1)',
                borderLeft: `2px solid ${LEVEL_COLOR[level]}`,
                borderRadius: 5,
                background: 'var(--bg-1)',
                padding: '8px 10px',
                display: 'grid',
                gap: 4,
              }}
            >
              <time style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10 }}>{entry.time}</time>
              <span style={{ color: 'var(--txt-1)', fontSize: 12 }}>{entry.message}</span>
            </li>
          );
        })}
      </ol>
    </section>
  );
}
