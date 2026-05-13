import React from 'react';
import { useTelemetryStore } from '../../store';

interface ConningBarProps {
  viewMode: 'captain' | 'god';
}

const CONNING_FIELDS = [
  { key: 'HDG', label: 'HDG', unit: '°' },
  { key: 'COG', label: 'COG', unit: '°' },
  { key: 'SOG', label: 'SOG', unit: 'kn' },
  { key: 'ROT', label: 'ROT', unit: '°/min' },
  { key: 'RUDDER', label: 'RUD', unit: '°' },
  { key: 'RPM', label: 'RPM', unit: '' },
  { key: 'PITCH', label: 'PITCH', unit: '°' },
];

function Sparkline({ data, color, w = 60, h = 16 }: { data: number[]; color: string; w?: number; h?: number }) {
  if (!data || data.length < 2) return <div style={{ width: w, height: h }} />;
  const min = Math.min(...data);
  const max = Math.max(...data);
  const range = max - min || 1;
  const pts = data.map((v, i) => `${(i / (data.length - 1)) * w},${h - ((v - min) / range) * (h - 2) - 1}`);
  const d = `M${pts.join(' L')}`;
  return (
    <svg width={w} height={h}>
      <path d={d} fill="none" stroke={color} strokeWidth="1" />
    </svg>
  );
}

export const ConningBar: React.FC<ConningBarProps> = ({ viewMode }) => {
  const ownShip = useTelemetryStore((s) => s.ownShip);
  const controlCmd = useTelemetryStore((s: any) => s.controlCmd);

  const sogKn = ownShip ? ((ownShip.kinematics?.sog ?? 0) * 1.944).toFixed(1) : '—';
  const cogDeg = ownShip ? (((ownShip.kinematics?.cog ?? 0) * 180 / Math.PI + 360) % 360).toFixed(0) : '—';
  const hdgDeg = ownShip ? (((ownShip.pose?.heading ?? 0) * 180 / Math.PI + 360) % 360).toFixed(0) : '—';
  const rotDpm = ownShip ? ((ownShip.kinematics?.rot ?? 0) * 180 / Math.PI * 60).toFixed(1) : '—';

  const fields: Record<string, string> = {
    HDG: hdgDeg,
    COG: cogDeg,
    SOG: sogKn,
    ROT: rotDpm,
    RUDDER: controlCmd?.rudder?.toFixed(1) ?? '—',
    RPM: controlCmd?.rpm?.toFixed(0) ?? '—',
    PITCH: controlCmd?.pitch?.toFixed(1) ?? '—',
  };

  const isGod = viewMode === 'god';

  return (
    <div data-testid="conning-bar" style={{
      position: 'absolute', bottom: 60, left: 16,
      zIndex: 16, display: 'flex', gap: isGod ? 8 : 4,
      background: 'rgba(11,19,32,0.88)', backdropFilter: 'blur(4px)',
      border: '1px solid var(--line-2)', padding: '4px 10px',
      fontFamily: 'var(--f-mono)',
    }}>
      {CONNING_FIELDS.map(({ key, label, unit }) => (
        <div key={key} style={{
          display: 'flex', flexDirection: isGod ? 'column' : 'row',
          alignItems: isGod ? 'stretch' : 'center',
          gap: isGod ? 4 : 6,
          padding: '0 6px',
          borderRight: '1px solid var(--line-1)',
          minWidth: isGod ? 70 : 50,
        }}>
          {isGod ? (
            <>
              <div style={{ display: 'flex', gap: 4, alignItems: 'baseline' }}>
                <span style={{ fontSize: 8, color: 'var(--txt-3)', letterSpacing: 0.5 }}>{label}</span>
                <span style={{ fontSize: 12, color: 'var(--c-phos)', fontWeight: 600 }}>
                  {fields[key]}{unit && <span style={{ fontSize: 8, color: 'var(--txt-3)' }}>{unit}</span>}
                </span>
              </div>
              <div data-testid="conning-history">
                <Sparkline data={[/* Phase 2: 60s ring buffer */]} color="var(--c-phos)" />
              </div>
            </>
          ) : (
            <>
              <span style={{ fontSize: 8, color: 'var(--txt-3)' }}>{label}</span>
              <span style={{ fontSize: 11, color: 'var(--c-phos)', fontWeight: 600 }}>
                {fields[key]}{unit}
              </span>
            </>
          )}
        </div>
      ))}
    </div>
  );
};
