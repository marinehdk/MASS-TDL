import React from 'react';
import { useTelemetryStore } from '../../store';

const SENSOR_LABELS = [
  { id: 'GNSS-A', name: 'GNSS-A', make: 'Trimble BD992' },
  { id: 'GNSS-B', name: 'GNSS-B', make: 'Septentrio mosaic' },
  { id: 'IMU',    name: 'IMU',    make: 'Xsens MTi-680G' },
  { id: 'RADAR',  name: 'RADAR',  make: 'JRC JMA-9230' },
  { id: 'AIS',    name: 'AIS',    make: 'Furuno FA-170' },
  { id: 'LIDAR',  name: 'LIDAR',  make: 'Velodyne VLS-128' },
  { id: 'CAM-FWD',name: 'CAM-F',  make: 'FLIR M364C' },
  { id: 'CAM-AFT',name: 'CAM-A',  make: 'Axis Q1798' },
];

const STATE_COLORS: Record<string, string> = {
  ok: 'var(--c-stbd)', warn: 'var(--c-warn)', fail: 'var(--c-danger)', disabled: 'var(--txt-3)',
};

export const SensorStatusRow: React.FC = () => {
  const sensors = useTelemetryStore((s) => (s as any).sensors ?? []);

  return (
    <div data-testid="preflight-sensors">
      <div style={{
        fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
        letterSpacing: '0.18em', textTransform: 'uppercase', marginBottom: 6,
      }}>SENSORS</div>
      <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
        {SENSOR_LABELS.map((s) => {
          const data = sensors.find((x: any) => x.id === s.id);
          const state = data?.state ?? 'disabled';
          const color = STATE_COLORS[state] ?? 'var(--txt-3)';
          return (
            <div key={s.id} title={`${s.make}\n${data?.payload ?? ''}`} style={{
              display: 'flex', alignItems: 'center', gap: 5,
              padding: '4px 8px', background: 'var(--bg-1)',
              border: `1px solid var(--line-1)`,
              borderLeft: `2px solid ${color}`,
            }}>
              <div style={{
                width: 7, height: 7, borderRadius: '50%', background: color,
                boxShadow: state === 'ok' ? `0 0 5px ${color}` : 'none',
              }} />
              <span style={{ fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-1)', letterSpacing: '0.06em' }}>
                {s.name}
              </span>
              <span style={{ fontFamily: 'var(--f-body)', fontSize: 8, color: 'var(--txt-3)' }}>
                {s.make}
              </span>
            </div>
          );
        })}
      </div>
    </div>
  );
};
