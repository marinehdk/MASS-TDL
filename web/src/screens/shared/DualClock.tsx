import React, { useState, useEffect } from 'react';

interface DualClockProps {
  simTime: number;
  showSim: boolean;
}

export const DualClock: React.FC<DualClockProps> = ({ simTime, showSim }) => {
  const [time, setTime] = useState(new Date());
  const [tz, setTz] = useState(8); // offset in hours, default to BJT (8)

  useEffect(() => {
    const id = setInterval(() => setTime(new Date()), 1000);
    return () => clearInterval(id);
  }, []);

  const tzTime = new Date(time.getTime() + tz * 3600 * 1000);
  const timeStr = tzTime.toISOString().slice(11, 19);

  const simM = Math.floor(simTime / 60).toString().padStart(2, '0');
  const simS = Math.floor(simTime % 60).toString().padStart(2, '0');

  return (
    <div style={{
      display: 'flex', alignItems: 'center', gap: 12,
      fontFamily: 'var(--f-mono)', fontSize: 16,
      padding: '4px 12px',
    }}>
      {showSim && (
        <>
          <div data-testid="dual-clock-sim" style={{
            display: 'flex', alignItems: 'center', gap: 8,
          }}>
            <span style={{ color: 'var(--c-info)', fontWeight: 700 }}>SIM T+</span>
            <span style={{ color: 'var(--txt-0)', fontWeight: 700, letterSpacing: '0.05em' }}>
              {simM}:{simS}
            </span>
          </div>
          <div style={{ width: 1, height: 16, background: 'var(--line-2)' }} />
        </>
      )}

      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <select
          value={tz}
          onChange={(e) => setTz(Number(e.target.value))}
          style={{
            background: 'transparent', border: 'none', 
            color: 'var(--c-info)',
            fontFamily: 'var(--f-mono)', fontSize: 16, cursor: 'pointer', outline: 'none',
            fontWeight: 700,
          }}
        >
          <option value={0}>UTC</option>
          <option value={8}>BJT</option>
          <option value={1}>CET</option>
          <option value={-5}>EST</option>
        </select>
        <div data-testid="dual-clock-utc" style={{
          color: 'var(--txt-0)',
          fontWeight: 700,
          letterSpacing: '0.05em'
        }}>
          {timeStr}
        </div>
      </div>
    </div>
  );
};
