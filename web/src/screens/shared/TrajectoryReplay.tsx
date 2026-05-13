import React, { useState, useMemo } from 'react';

interface TrajectoryReplayProps {
  durationSec: number;
  currentTimeSec: number;
  onTimeChange?: (t: number) => void;
}

export const TrajectoryReplay: React.FC<TrajectoryReplayProps> = ({
  durationSec, currentTimeSec, onTimeChange,
}) => {
  const [playing, setPlaying] = useState(false);
  const [rate, setRate] = useState(1);

  const progress = durationSec > 0 ? currentTimeSec / durationSec : 0;

  // Generate simulated ownship trajectory (straight north then jog right)
  const ownshipPts = useMemo(() => {
    const N = 60;
    return Array.from({ length: N }, (_, i) => {
      const u = i / (N - 1);
      const x = 80 + u * 360 + (u > 0.4 && u < 0.7 ? 50 * Math.sin((u - 0.4) * Math.PI / 0.3) : 0);
      const y = 320 - u * 280;
      return [x, y] as [number, number];
    });
  }, []);

  const t01Pts = useMemo(() => ownshipPts.map(([x, y], i) =>
    [x + 120 - i * 1.6, y - 80 + i * 1.0] as [number, number]
  ), [ownshipPts]);

  const visIdx = Math.floor(progress * (ownshipPts.length - 1));
  const visOwn = ownshipPts.slice(0, visIdx + 1);
  const visT01 = t01Pts.slice(0, visIdx + 1);
  const cur = ownshipPts[Math.min(visIdx, ownshipPts.length - 1)];

  const fmtT = (t: number) => `T+${String(Math.floor(t / 60)).padStart(2, '0')}:${String(t % 60).padStart(2, '0')}`;

  return (
    <div data-testid="trajectory-replay" style={{
      display: 'flex', flexDirection: 'column', height: '100%',
      background: 'var(--bg-1)', border: '1px solid var(--line-1)',
    }}>
      <div style={{
        fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
        letterSpacing: '0.16em', textTransform: 'uppercase',
        padding: '6px 8px', borderBottom: '1px solid var(--line-1)',
      }}>
        TRAJECTORY REPLAY
      </div>

      {/* Map area */}
      <div style={{ flex: 1, position: 'relative' }}>
        <svg viewBox="0 0 480 360" style={{ width: '100%', height: '100%', background: '#050810' }}>
          {/* Grid */}
          {[0, 1, 2, 3, 4, 5].map(i => (
            <line key={'h' + i} x1="0" y1={60 * i} x2="480" y2={60 * i} stroke="var(--line-1)" strokeWidth="0.4" />
          ))}
          {[0, 1, 2, 3, 4, 5, 6, 7, 8].map(i => (
            <line key={'v' + i} x1={60 * i} y1="0" x2={60 * i} y2="360" stroke="var(--line-1)" strokeWidth="0.4" />
          ))}

          {/* Planned path */}
          <path d={ownshipPts.map(([x, y], i) => (i ? 'L' : 'M') + x + ' ' + y).join(' ')}
            stroke="var(--line-3)" strokeWidth="1" strokeDasharray="3 4" fill="none" />

          {/* T01 ghost path */}
          <path d={t01Pts.map(([x, y], i) => (i ? 'L' : 'M') + x + ' ' + y).join(' ')}
            stroke="var(--c-danger)" strokeWidth="0.6" strokeDasharray="2 3" fill="none" opacity="0.4" />

          {/* Visited paths */}
          <path d={visOwn.map(([x, y], i) => (i ? 'L' : 'M') + x + ' ' + y).join(' ')}
            stroke="var(--c-phos)" strokeWidth="1.5" fill="none" />
          <path d={visT01.map(([x, y], i) => (i ? 'L' : 'M') + x + ' ' + y).join(' ')}
            stroke="var(--c-danger)" strokeWidth="1.2" fill="none" />

          {/* Current ownship position */}
          {cur && (
            <g transform={`translate(${cur[0]},${cur[1]})`}>
              <circle cx="0" cy="0" r="14" fill="none" stroke="var(--c-phos)" strokeWidth="0.6" opacity="0.4" />
              <path d="M 0 -8 L 5 5 L 0 2 L -5 5 Z" fill="var(--c-phos)" />
            </g>
          )}

          {/* Labels */}
          <text x="6" y="350" fontFamily="var(--f-mono)" fontSize="9" fill="var(--txt-3)">0 nm</text>
          <text x="440" y="350" fontFamily="var(--f-mono)" fontSize="9" fill="var(--txt-3)">6 nm</text>
          <text x="78" y="335" fontFamily="var(--f-body)" fontSize="9" fill="var(--c-phos)">OWN</text>
          {visT01.length > 0 && (
            <text x={visT01[visT01.length - 1][0] + 8} y={visT01[visT01.length - 1][1] - 4}
              fontFamily="var(--f-body)" fontSize="8" fill="var(--c-danger)">T01</text>
          )}
        </svg>
      </div>

      {/* Playback controls */}
      <div style={{
        display: 'flex', alignItems: 'center', gap: 6, padding: '6px 8px',
        borderTop: '1px solid var(--line-1)',
      }}>
        <button onClick={() => setPlaying(!playing)} style={{
          background: 'transparent', border: '1px solid var(--line-2)',
          color: 'var(--txt-1)', padding: '2px 8px', cursor: 'pointer',
          fontFamily: 'var(--f-mono)', fontSize: 10,
        }}>
          {playing ? '⏸' : '▶'}
        </button>
        {[0.5, 1, 2, 4, 10].map(r => (
          <button key={r} onClick={() => setRate(r)} style={{
            background: rate === r ? 'rgba(91,192,190,0.15)' : 'transparent',
            border: `1px solid ${rate === r ? 'var(--c-phos)' : 'var(--line-2)'}`,
            color: rate === r ? 'var(--c-phos)' : 'var(--txt-3)',
            padding: '2px 6px', cursor: 'pointer',
            fontFamily: 'var(--f-mono)', fontSize: 9,
          }}>&times;{r}</button>
        ))}
        <div style={{ flex: 1 }} />
        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--c-phos)' }}>
          {fmtT(currentTimeSec)}
        </span>
      </div>
    </div>
  );
};
