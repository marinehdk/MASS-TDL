import React, { useState, useEffect } from 'react';

interface DualClockProps {
  simTime: number;
  showSim: boolean;
}

export const DualClock: React.FC<DualClockProps> = ({ simTime, showSim }) => {
  const [time, setTime] = useState(new Date());
  const [tz, setTz] = useState(0); // offset in hours

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
      fontFamily: 'var(--f-mono)', fontSize: 12,
      background: 'var(--bg-0)',
      border: '1px solid var(--line-2)',
      borderRadius: 4,
      padding: showSim ? '4px 12px 4px 6px' : '4px 12px',
    }}>
      {showSim && (
        <>
          <div data-testid="dual-clock-sim" style={{
            color: 'var(--bg-0)',
            background: 'var(--c-phos)',
            padding: '2px 8px', borderRadius: 3,
            fontWeight: 700,
            boxShadow: '0 0 10px rgba(91,192,190,0.5)',
          }}>
            SIM T+{simM}:{simS}
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
            color: showSim ? 'var(--txt-3)' : 'var(--c-info)',
            fontFamily: 'var(--f-mono)', fontSize: 12, cursor: 'pointer', outline: 'none',
            fontWeight: showSim ? 500 : 700,
          }}
        >
          <option value={0}>UTC</option>
          <option value={8}>BJT</option>
          <option value={1}>CET</option>
          <option value={-5}>EST</option>
        </select>
        <div data-testid="dual-clock-utc" style={{
          color: showSim ? 'var(--txt-2)' : 'var(--txt-0)',
          fontWeight: showSim ? 500 : 700,
          letterSpacing: '0.05em'
        }}>
          {timeStr}
        </div>
      </div>
    </div>
  );
};
