import type { RuntimeCoreService } from '../../api/silApi';

export function CoreServicePanel({
  services,
  onRestart,
  onStopCoreStack,
}: {
  services: RuntimeCoreService[];
  onRestart: (service: string) => void;
  onStopCoreStack: () => void;
}) {
  return (
    <section style={{ display: 'grid', gap: 12 }}>
      <div style={{ display: 'grid', gap: 10 }}>
        {services.map((service) => (
          <article
            key={service.id}
            style={{
              border: '1px solid var(--line-1)',
              borderRadius: 6,
              background: 'var(--bg-1)',
              padding: 12,
              display: 'grid',
              gap: 10,
            }}
          >
            <div style={{ display: 'flex', justifyContent: 'space-between', gap: 10, alignItems: 'start' }}>
              <div style={{ display: 'grid', gap: 4, minWidth: 0 }}>
                <strong style={{ color: 'var(--txt-0)', fontSize: 14 }}>{service.service}</strong>
                <span style={{ color: 'var(--txt-2)', fontFamily: 'var(--f-mono)', fontSize: 11, wordBreak: 'break-all' }}>
                  {service.container_name}
                </span>
              </div>
              <span style={{ color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', fontSize: 11 }}>
                {service.status} / {service.health}
              </span>
            </div>
            <div style={{ color: 'var(--txt-3)', fontFamily: 'var(--f-mono)', fontSize: 11, wordBreak: 'break-all' }}>
              {service.image}
            </div>
            <div>
              {service.allowed_actions.includes('restart') && (
                <button type="button" onClick={() => onRestart(service.service)}>
                  Restart {service.service}
                </button>
              )}
            </div>
          </article>
        ))}
      </div>
      <button type="button" onClick={onStopCoreStack}>
        Stop Core Stack
      </button>
    </section>
  );
}
