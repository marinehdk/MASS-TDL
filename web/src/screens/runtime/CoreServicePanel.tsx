import type { RuntimeCoreService } from '../../api/silApi';

export function CoreServicePanel({
  services,
  onRestart,
}: {
  services: RuntimeCoreService[];
  onRestart: (service: string) => void;
}) {
  return (
    <div data-testid="runtime-core-grid" style={{ display: 'grid', gridTemplateColumns: 'repeat(2, minmax(0, 1fr))', gap: 10 }}>
      {services.map((service) => (
        (() => {
          const running = service.status === 'running';
          return (
        <article
          key={service.id}
          data-testid={`core-service-card-${service.id}`}
          style={{
            minHeight: 118,
            border: '1px solid var(--line-1)',
            borderRadius: 8,
            background: 'rgba(10, 15, 24, 0.86)',
            padding: 12,
            display: 'grid',
            gridTemplateRows: 'auto 1fr auto',
            gap: 8,
          }}
        >
          <div style={{ display: 'flex', justifyContent: 'space-between', gap: 10, alignItems: 'start' }}>
            <div style={{ display: 'grid', gap: 4, minWidth: 0 }}>
              <strong style={{ color: 'var(--txt-0)', fontSize: 14 }}>{service.service}</strong>
              <span style={{ color: 'var(--txt-2)', fontFamily: 'var(--f-mono)', fontSize: 10, wordBreak: 'break-all' }}>
                {service.container_name}
              </span>
            </div>
            <span
              style={{
                color: running ? 'var(--c-stbd)' : 'var(--c-danger)',
                border: `1px solid ${running ? 'var(--c-stbd)' : 'var(--c-danger)'}`,
                borderRadius: 4,
                padding: '2px 6px',
                background: running ? 'rgba(0, 227, 179, 0.12)' : 'rgba(248, 81, 73, 0.12)',
                fontFamily: 'var(--f-mono)',
                fontSize: 10,
                fontWeight: 800,
                whiteSpace: 'nowrap',
              }}
            >
              {running ? '运行' : '停止'}
            </span>
          </div>
          <div style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 10, wordBreak: 'break-all' }}>
            {service.image}
          </div>
          {service.allowed_actions.includes('restart') && (
            <button
              type="button"
              onClick={() => onRestart(service.service)}
              style={{
                justifySelf: 'start',
                minWidth: 72,
                minHeight: 30,
                border: '1px solid var(--c-info)',
                borderRadius: 5,
                background: 'var(--c-info)',
                color: 'var(--bg-0)',
                fontWeight: 800,
                cursor: 'pointer',
              }}
            >
              重启
            </button>
          )}
        </article>
          );
        })()
      ))}
    </div>
  );
}
