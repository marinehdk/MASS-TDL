import { useMemo, useState } from 'react';
import { Activity, PlugZap } from 'lucide-react';
import {
  useGetIntegrationStatusQuery,
  useListIntegrationProfilesQuery,
  useProbeIntegrationMutation,
  useSelectIntegrationProfileMutation,
  type IntegrationProbeResult,
  type IntegrationProfileEntry,
} from '../../api/silApi';

function profileName(profile: IntegrationProfileEntry): string {
  return typeof profile === 'string' ? profile : profile.name;
}

function profileMode(profile: IntegrationProfileEntry): string {
  if (typeof profile === 'string') return profile === 'default' ? 'default' : 'external';
  return profile.mode ?? (profile.external_enabled ? 'external' : 'default');
}

export function ExternalIntegrationPanel() {
  const { data: profilesData } = useListIntegrationProfilesQuery();
  const { data: status } = useGetIntegrationStatusQuery();
  const [selectProfile] = useSelectIntegrationProfileMutation();
  const [probeIntegration] = useProbeIntegrationMutation();
  const [probe, setProbe] = useState<IntegrationProbeResult | null>(null);
  const [error, setError] = useState('');

  const profiles = profilesData?.profiles ?? [];
  const active = status?.active_profile ?? profilesData?.active_profile ?? 'default';
  const activeMode = useMemo(() => {
    const profile = profiles.find((item) => profileName(item) === active);
    return profile ? profileMode(profile) : status?.external_enabled ? 'external' : 'default';
  }, [active, profiles, status?.external_enabled]);

  const runProbe = async () => {
    setError('');
    try {
      const result = await probeIntegration().unwrap();
      setProbe(result);
    } catch (e) {
      setError(`Probe failed: ${e instanceof Error ? e.message : String(e)}`);
    }
  };

  const onProfileChange = async (name: string) => {
    setError('');
    setProbe(null);
    try {
      await selectProfile({ name }).unwrap();
    } catch (e) {
      setError(`Profile select failed: ${e instanceof Error ? e.message : String(e)}`);
    }
  };

  return (
    <section
      data-testid="external-integration-panel"
      style={{
        borderTop: '1px solid var(--line-1)',
        padding: 12,
        fontFamily: 'var(--f-body)',
        color: 'var(--txt-1)',
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 8 }}>
        <h3 style={{ margin: 0, fontSize: 13, fontWeight: 700, letterSpacing: 0 }}>
          External Integration
        </h3>
        <span
          data-testid="integration-active-profile"
          style={{
            fontFamily: 'var(--f-mono)',
            fontSize: 11,
            color: status?.external_enabled ? 'var(--c-warn)' : 'var(--txt-3)',
          }}
        >
          {active}
        </span>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr auto', gap: 8, alignItems: 'center', marginTop: 10 }}>
        <select
          data-testid="integration-profile-select"
          value={active}
          onChange={(event) => onProfileChange(event.target.value)}
          style={{
            width: '100%',
            height: 32,
            minWidth: 0,
            background: 'var(--bg-1)',
            color: 'var(--txt-1)',
            border: '1px solid var(--line-1)',
            borderRadius: 4,
            fontFamily: 'var(--f-mono)',
            fontSize: 11,
          }}
        >
          {profiles.map((profile) => {
            const name = profileName(profile);
            return <option key={name} value={name}>{name}</option>;
          })}
        </select>
        <span
          data-testid="integration-mode-pill"
          style={{
            display: 'inline-flex',
            alignItems: 'center',
            gap: 4,
            minWidth: 70,
            justifyContent: 'center',
            height: 30,
            border: '1px solid var(--line-1)',
            borderRadius: 4,
            fontFamily: 'var(--f-mono)',
            fontSize: 10,
            color: activeMode === 'external' ? 'var(--c-warn)' : 'var(--txt-3)',
          }}
        >
          <PlugZap size={13} aria-hidden="true" />
          {activeMode}
        </span>
      </div>

      <button
        data-testid="integration-probe-button"
        type="button"
        onClick={runProbe}
        style={{
          width: '100%',
          marginTop: 10,
          height: 34,
          display: 'inline-flex',
          alignItems: 'center',
          justifyContent: 'center',
          gap: 6,
          background: 'var(--bg-2)',
          color: 'var(--txt-1)',
          border: '1px solid var(--line-1)',
          borderRadius: 4,
          fontSize: 12,
          fontWeight: 700,
        }}
      >
        <Activity size={14} aria-hidden="true" />
        Probe External Gates
      </button>

      {error && (
        <div
          style={{
            marginTop: 8,
            color: 'var(--c-danger)',
            fontFamily: 'var(--f-mono)',
            fontSize: 11,
            lineHeight: 1.35,
          }}
        >
          {error}
        </div>
      )}

      <div style={{ marginTop: 10, display: 'grid', gap: 6 }}>
        {(probe?.checks ?? []).map((check) => (
          <div
            key={`${check.gate_id}-${check.label}`}
            style={{ display: 'grid', gridTemplateColumns: '28px 1fr', gap: 6, alignItems: 'center', fontSize: 11 }}
          >
            <span
              style={{
                fontFamily: 'var(--f-mono)',
                color: check.passed ? 'var(--c-stbd)' : 'var(--c-danger)',
              }}
            >
              {check.passed ? 'OK' : 'NO'}
            </span>
            <span title={check.detail}>{check.label}</span>
          </div>
        ))}
      </div>
    </section>
  );
}
