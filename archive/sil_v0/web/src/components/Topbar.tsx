import { memo } from 'react';
import type { SilDebugData } from '../types/sil';

interface Props {
  data: SilDebugData | null;
}

/**
 * Topbar component: Displays system state, connection status, and key metrics.
 * Designed with a premium dark-mode aesthetic consistent with maritime SA standards.
 */
export const Topbar = memo(function Topbar({ data }: Props) {
  const d = data;
  
  // Connection status indicator logic (usually handled via App.tsx but reflected here)
  const isLive = !!d && d.job_status === 'running';

  return (
    <div style={{
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between',
      padding: '0 20px',
      background: 'var(--bg-1)',
      borderBottom: '1px solid var(--line-1)',
      fontFamily: 'var(--fnt-sans)',
      color: 'var(--fg-1)',
    }}>
      {/* Left side: Branding & Scenario Info */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '24px' }}>
        <div style={{
          fontSize: '18px',
          fontWeight: 700,
          letterSpacing: '0.05em',
          color: 'var(--color-stbd)',
          textTransform: 'uppercase'
        }}>
          MASS L3 <span style={{ color: 'var(--fg-0)', fontWeight: 300 }}>TDL SIL</span>
        </div>
        
        <div style={{ display: 'flex', flexDirection: 'column' }}>
          <span style={{ fontSize: '10px', color: 'var(--fg-3)', textTransform: 'uppercase' }}>Active Scenario</span>
          <span style={{ fontSize: '12px', fontWeight: 500, color: 'var(--fg-1)' }}>
            {d?.scenario_id || 'Waiting for scenario...'}
          </span>
        </div>
      </div>

      {/* Middle: Critical METs */}
      <div style={{ display: 'flex', gap: '40px' }}>
        <MetItem label="CPA" value={d?.cpa_nm?.toFixed(2) || '0.00'} unit="NM" />
        <MetItem label="TCPA" value={d?.tcpa_s?.toFixed(0) || '0'} unit="S" />
        <MetItem label="TMR" value={d?.sat3_tmr_s?.toFixed(0) || '60'} unit="S" color="var(--color-warn)" />
      </div>

      {/* Right side: Connection Status */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
        <div style={{
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'flex-end'
        }}>
          <span id="bridge-status-text" style={{ 
            fontSize: '11px', 
            fontWeight: 600,
            color: d ? 'var(--color-stbd)' : 'var(--color-port)'
          }}>
            {d ? 'SIMULATION ACTIVE' : 'DISCONNECTED'}
          </span>
          <span style={{ fontSize: '9px', color: 'var(--fg-3)' }}>
            {d ? 'Uptime: 04:12:05' : 'Check backend /orbstack'}
          </span>
        </div>
        <div style={{
          width: '8px',
          height: '8px',
          borderRadius: '50%',
          background: d ? 'var(--color-stbd)' : 'var(--color-port)',
          boxShadow: d ? '0 0 8px var(--color-stbd)' : 'none'
        }} />
      </div>
    </div>
  );
});

function MetItem({ label, value, unit, color }: { label: string, value: string, unit: string, color?: string }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
      <span style={{ fontSize: '9px', color: 'var(--fg-3)', textTransform: 'uppercase' }}>{label}</span>
      <div style={{ display: 'flex', alignItems: 'baseline', gap: '2px' }}>
        <span style={{ fontSize: '16px', fontWeight: 600, color: color || 'var(--fg-1)', fontFamily: 'var(--fnt-mono)' }}>{value}</span>
        <span style={{ fontSize: '9px', fontWeight: 500, color: 'var(--fg-3)' }}>{unit}</span>
      </div>
    </div>
  );
}
