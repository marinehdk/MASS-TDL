import React, { useState, useEffect } from 'react';

interface DualClockProps {
  simTime: number;
}

export const DualClock: React.FC<DualClockProps> = ({ simTime }) => {
  const [utcTime, setUtcTime] = useState(new Date());

  useEffect(() => {
    const id = setInterval(() => setUtcTime(new Date()), 1000);
    return () => clearInterval(id);
  }, []);

  const utcStr = utcTime.toISOString().slice(11, 19) + 'Z';
  const simM = Math.floor(simTime / 60).toString().padStart(2, '0');
  const simS = Math.floor(simTime % 60).toString().padStart(2, '0');

  return (
    <div style={{
      display: 'flex', alignItems: 'center', gap: 12,
      fontFamily: 'var(--f-mono)', fontSize: 11,
    }}>
      <div data-testid="dual-clock-sim" style={{ color: 'var(--c-phos)', fontWeight: 600 }}>
        SIM T+{simM}:{simS}
      </div>
      <div style={{ width: 1, height: 14, background: 'var(--line-2)' }} />
      <div data-testid="dual-clock-utc" style={{ color: 'var(--txt-2)' }}>
        UTC {utcStr}
      </div>
    </div>
  );
};
