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

  React.useEffect(() => {
    if (!playing || !onTimeChange) return;
    const interval = setInterval(() => {
      const next = currentTimeSec + 0.1 * rate;
      if (next >= durationSec) {
        onTimeChange(durationSec);
        setPlaying(false);
      } else {
        onTimeChange(next);
      }
    }, 100);
    return () => clearInterval(interval);
  }, [playing, rate, durationSec, currentTimeSec, onTimeChange]);

  const handlePlayClick = () => {
    if (currentTimeSec >= durationSec && onTimeChange) {
      onTimeChange(0);
    }
    setPlaying(!playing);
  };

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
  const nextPt = ownshipPts[Math.min(visIdx + 1, ownshipPts.length - 1)];
  const angleRad = cur && nextPt && (nextPt[0] !== cur[0] || nextPt[1] !== cur[1])
    ? Math.atan2(nextPt[1] - cur[1], nextPt[0] - cur[0])
    : -Math.PI / 2;
  const angleDeg = (angleRad * 180) / Math.PI + 90;

  const curT01 = t01Pts[Math.min(visIdx, t01Pts.length - 1)];
  const nextT01Pt = t01Pts[Math.min(visIdx + 1, t01Pts.length - 1)];
  const angleT01Rad = curT01 && nextT01Pt && (nextT01Pt[0] !== curT01[0] || nextT01Pt[1] !== curT01[1])
    ? Math.atan2(nextT01Pt[1] - curT01[1], nextT01Pt[0] - curT01[0])
    : -Math.PI / 2;
  const angleT01Deg = (angleT01Rad * 180) / Math.PI + 90;

  const fmtT = (t: number) => `T+${String(Math.floor(t / 60)).padStart(2, '0')}:${String(t % 60).padStart(2, '0')}`;

  return (
    <div data-testid="trajectory-replay" style={{
      display: 'flex', flexDirection: 'column', height: '100%',
      background: 'var(--bg-1)', border: '1px solid var(--line-1)',
    }}>
      <div style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        padding: '6px 8px', borderBottom: '1px solid var(--line-1)',
      }}>
        <span style={{
          fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
          letterSpacing: '0.16em', textTransform: 'uppercase',
        }}>
          TRAJECTORY REPLAY
        </span>
        <span style={{
          fontFamily: 'var(--f-mono)', fontSize: 8, color: 'var(--c-warn)',
          border: '1px solid var(--c-warn)', borderRadius: 2, padding: '0 4px',
          letterSpacing: '0.05em',
        }}>
          [DEMO DATA - MOCK ACTIVE]
        </span>
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

          {/* Current ownship position with 3-Tier Safety Domain */}
          {cur && (
            <g transform={`translate(${cur[0]},${cur[1]}) rotate(${angleDeg})`}>
              {/* Tier 1: Observation Zone */}
              <ellipse cx="0" cy="0" rx="32" ry="50" fill="none" stroke="var(--c-info)" strokeWidth="0.8" strokeDasharray="3 3" opacity="0.5" />
              {/* Tier 2: Action Zone */}
              <ellipse cx="0" cy="0" rx="18" ry="28" fill="rgba(212, 175, 55, 0.08)" stroke="rgba(212, 175, 55, 0.4)" strokeWidth="1" />
              {/* Tier 3: Critical Zone */}
              <ellipse cx="0" cy="0" rx="8" ry="14" fill="rgba(217, 83, 79, 0.05)" stroke="var(--c-danger)" strokeWidth="1.2" />
              
              <path d="M 0 -8 L 4 5 L 0 2 L -4 5 Z" fill="var(--c-phos)" />
            </g>
          )}

          {/* Current T01 target ship position */}
          {curT01 && (
            <g transform={`translate(${curT01[0]},${curT01[1]}) rotate(${angleT01Deg})`}>
              <circle cx="0" cy="0" r="10" fill="none" stroke="var(--c-danger)" strokeWidth="0.6" opacity="0.3" />
              <path d="M 0 -6 L 3 4 L 0 1.5 L -3 4 Z" fill="var(--c-danger)" />
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
        <button onClick={handlePlayClick} style={{
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
        <input
          type="range"
          min={0}
          max={durationSec}
          value={currentTimeSec}
          onChange={(e) => onTimeChange?.(Number(e.target.value))}
          style={{
            flex: 1,
            margin: '0 12px',
            accentColor: 'var(--c-phos)',
            background: 'var(--line-2)',
            height: 4,
            borderRadius: 2,
            cursor: 'pointer',
          }}
        />
        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 10, color: 'var(--c-phos)' }}>
          {fmtT(currentTimeSec)}
        </span>
      </div>
    </div>
  );
};
