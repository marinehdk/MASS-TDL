import { useEffect, useRef, useState } from 'react';
import type { RuntimePlugin, RuntimePluginRole, RuntimePluginRoleName } from '../../api/silApi';

const ROLE_LABELS: Record<RuntimePluginRoleName, string> = {
  hydrodynamics: '水动力模型',
  route_l2: 'L2 航线规划',
  fusion: '融合感知',
};

function PluginCard({ plugin, active }: { plugin: RuntimePlugin; active: boolean }) {
  return (
    <article
      style={{
        border: `1px solid ${active ? 'var(--c-info)' : 'var(--line-1)'}`,
        borderRadius: 6,
        background: active ? 'var(--bg-2)' : 'var(--bg-1)',
        padding: 12,
        display: 'grid',
        gap: 8,
      }}
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', gap: 10, alignItems: 'start' }}>
        <div style={{ display: 'grid', gap: 4, minWidth: 0 }}>
          <strong style={{ color: 'var(--txt-0)', fontSize: 14 }}>{plugin.label}</strong>
          <span style={{ color: 'var(--txt-2)', fontFamily: 'var(--f-mono)', fontSize: 11, wordBreak: 'break-all' }}>
            {plugin.service}
          </span>
        </div>
        <span style={{ color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', fontSize: 11 }}>
          {plugin.status} / {plugin.health}
        </span>
      </div>
      <dl style={{ display: 'grid', gridTemplateColumns: 'auto 1fr', gap: '4px 10px', margin: 0, fontSize: 11 }}>
        <dt style={{ color: 'var(--txt-3)' }}>Container</dt>
        <dd style={{ margin: 0, color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', wordBreak: 'break-all' }}>
          {plugin.container || '-'}
        </dd>
        <dt style={{ color: 'var(--txt-3)' }}>Image</dt>
        <dd style={{ margin: 0, color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', wordBreak: 'break-all' }}>
          {plugin.image}
        </dd>
        <dt style={{ color: 'var(--txt-3)' }}>Expected</dt>
        <dd style={{ margin: 0, color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', wordBreak: 'break-all' }}>
          {plugin.expected_image}
        </dd>
        <dt style={{ color: 'var(--txt-3)' }}>Revision</dt>
        <dd style={{ margin: 0, color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', wordBreak: 'break-all' }}>
          {plugin.revision}
        </dd>
        <dt style={{ color: 'var(--txt-3)' }}>ROS Domain</dt>
        <dd style={{ margin: 0, color: 'var(--txt-1)', fontFamily: 'var(--f-mono)' }}>{plugin.ros_domain_id}</dd>
        <dt style={{ color: 'var(--txt-3)' }}>Topics</dt>
        <dd style={{ margin: 0, color: 'var(--txt-1)', fontFamily: 'var(--f-mono)', display: 'grid', gap: 3 }}>
          {Object.entries(plugin.required_topics).map(([topic, type]) => (
            <span key={topic} style={{ wordBreak: 'break-all' }}>
              {topic} · {type}
            </span>
          ))}
        </dd>
      </dl>
    </article>
  );
}

export function PluginRolePanel({
  role,
  onSwitch,
}: {
  role: RuntimePluginRole;
  onSwitch: (role: RuntimePluginRoleName, pluginId: string) => void;
}) {
  const selectId = `${role.role}-runtime-plugin`;
  const initialPluginId = role.active_plugin ?? role.plugins[0]?.id ?? '';
  const [selectedPluginId, setSelectedPluginId] = useState(initialPluginId);
  const previousActivePluginRef = useRef(role.active_plugin);
  const pluginIdsKey = role.plugins.map((plugin) => plugin.id).join('\u0000');

  useEffect(() => {
    const activeChanged = previousActivePluginRef.current !== role.active_plugin;
    previousActivePluginRef.current = role.active_plugin;
    setSelectedPluginId((current) => {
      const fallbackPluginId = role.active_plugin ?? role.plugins[0]?.id ?? '';
      if (activeChanged) {
        return fallbackPluginId;
      }
      if (current && role.plugins.some((plugin) => plugin.id === current)) {
        return current;
      }
      return fallbackPluginId;
    });
  }, [role.active_plugin, pluginIdsKey]);

  return (
    <section style={{ display: 'grid', gap: 12 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', gap: 12, alignItems: 'end', flexWrap: 'wrap' }}>
        <label htmlFor={selectId} style={{ display: 'grid', gap: 6, color: 'var(--txt-1)', fontWeight: 800 }}>
          {ROLE_LABELS[role.role]}
          <select
            id={selectId}
            value={selectedPluginId}
            onChange={(event) => setSelectedPluginId(event.target.value)}
            style={{
              minWidth: 220,
              height: 34,
              border: '1px solid var(--line-1)',
              borderRadius: 5,
              background: 'var(--bg-1)',
              color: 'var(--txt-1)',
            }}
          >
            {role.plugins.map((plugin) => (
              <option key={plugin.id} value={plugin.id}>
                {plugin.label}
              </option>
            ))}
          </select>
        </label>
        {selectedPluginId && selectedPluginId !== role.active_plugin && (
          <button type="button" onClick={() => onSwitch(role.role, selectedPluginId)}>
            Switch {role.role}
          </button>
        )}
      </div>

      <div style={{ display: 'grid', gap: 10 }}>
        {role.plugins.map((plugin) => (
          <PluginCard key={plugin.id} plugin={plugin} active={plugin.id === role.active_plugin} />
        ))}
      </div>
    </section>
  );
}
