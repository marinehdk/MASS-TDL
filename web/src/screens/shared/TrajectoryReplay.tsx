import React, { useState, useMemo } from 'react';
import type { EvidenceReplayTrajectoryPoint } from '../../api/silApi';

interface TrajectoryReplayProps {
  durationSec: number;
  currentTimeSec: number;
  onTimeChange?: (t: number) => void;
  points?: EvidenceReplayTrajectoryPoint[];
}

const project = (
  lat: number,
  lon: number,
  bounds: { minLat: number; maxLat: number; minLon: number; maxLon: number },
): [number, number] => {
  const lonSpan = Math.max(0.000001, bounds.maxLon - bounds.minLon);
  const latSpan = Math.max(0.000001, bounds.maxLat - bounds.minLat);
  return [
    40 + ((lon - bounds.minLon) / lonSpan) * 400,
    320 - ((lat - bounds.minLat) / latSpan) * 280,
  ];
};

const hasPosition = (point: EvidenceReplayTrajectoryPoint) =>
  typeof point.lat === 'number' && typeof point.lon === 'number';

const isOwnship = (point: EvidenceReplayTrajectoryPoint) =>
  point.vessel_role === 'ownship' || point.vessel_id === 'OWN';

type ProjectedPoint = { sim_t: number; x: number; y: number };

const pointAtTime = (track: ProjectedPoint[], simT: number): ProjectedPoint | undefined => {
  if (track.length === 0) return undefined;
  if (simT <= track[0].sim_t) return track[0];
  if (simT >= track[track.length - 1].sim_t) return track[track.length - 1];
  for (let index = 1; index < track.length; index += 1) {
    const next = track[index];
    if (next.sim_t < simT) continue;
    const prev = track[index - 1];
    const span = Math.max(0.000001, next.sim_t - prev.sim_t);
    const frac = (simT - prev.sim_t) / span;
    return {
      sim_t: simT,
      x: prev.x + (next.x - prev.x) * frac,
      y: prev.y + (next.y - prev.y) * frac,
    };
  }
  return track[track.length - 1];
};

const visibleTrack = (track: ProjectedPoint[], simT: number): [number, number][] => {
  const visible = track
    .filter((point) => point.sim_t <= simT)
    .map((point) => [point.x, point.y] as [number, number]);
  const current = pointAtTime(track, simT);
  if (current && !visible.some(([x, y]) => x === current.x && y === current.y)) {
    visible.push([current.x, current.y]);
  }
  return visible;
};

export const TrajectoryReplay: React.FC<TrajectoryReplayProps> = ({
  durationSec, currentTimeSec, onTimeChange, points,
}) => {
  const [playing, setPlaying] = useState(false);
  const [rate, setRate] = useState(1);

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

  const dataPoints = points ?? [];
  const bounds = useMemo(() => {
    const valid = dataPoints.filter((p) => typeof p.lat === 'number' && typeof p.lon === 'number');
    if (valid.length === 0) return { minLat: 0, maxLat: 1, minLon: 0, maxLon: 1 };
    const lats = valid.map((p) => p.lat as number);
    const lons = valid.map((p) => p.lon as number);
    return {
      minLat: Math.min(...lats),
      maxLat: Math.max(...lats),
      minLon: Math.min(...lons),
      maxLon: Math.max(...lons),
    };
  }, [dataPoints]);

  const makeFallbackTrack = (): ProjectedPoint[] => {
    const N = 60;
    return Array.from({ length: N }, (_, i) => {
      const u = i / (N - 1);
      const x = 80 + u * 360 + (u > 0.4 && u < 0.7 ? 50 * Math.sin((u - 0.4) * Math.PI / 0.3) : 0);
      const y = 320 - u * 280;
      return { sim_t: u * durationSec, x, y };
    });
  };

  const ownshipTrack = useMemo(() => {
    if (dataPoints.length > 0) {
      return dataPoints
        .filter((p) => isOwnship(p) && hasPosition(p))
        .sort((a, b) => a.sim_t - b.sim_t)
        .map((p) => {
          const [x, y] = project(p.lat as number, p.lon as number, bounds);
          return { sim_t: p.sim_t, x, y };
        });
    }
    return makeFallbackTrack();
  }, [dataPoints, bounds, durationSec]);

  const targetTrack = useMemo(() => {
    if (dataPoints.length > 0) {
      const targetId = dataPoints.find((p) => !isOwnship(p) && hasPosition(p))?.vessel_id;
      return dataPoints
        .filter((p) => !isOwnship(p) && hasPosition(p) && (!targetId || p.vessel_id === targetId))
        .sort((a, b) => a.sim_t - b.sim_t)
        .map((p) => {
          const [x, y] = project(p.lat as number, p.lon as number, bounds);
          return { sim_t: p.sim_t, x, y };
        });
    }
    return ownshipTrack.map((point, i) =>
      ({ sim_t: point.sim_t, x: point.x + 120 - i * 1.6, y: point.y - 80 + i * 1.0 })
    );
  }, [dataPoints, ownshipTrack, bounds]);

  const ownshipPts = ownshipTrack.map((point) => [point.x, point.y] as [number, number]);
  const t01Pts = targetTrack.map((point) => [point.x, point.y] as [number, number]);
  const visOwn = visibleTrack(ownshipTrack, currentTimeSec);
  const visT01 = visibleTrack(targetTrack, currentTimeSec);
  const curPoint = pointAtTime(ownshipTrack, currentTimeSec);
  const nextPoint = pointAtTime(ownshipTrack, currentTimeSec + Math.max(0.1, durationSec * 0.01));
  const cur = curPoint ? [curPoint.x, curPoint.y] as [number, number] : undefined;
  const nextPt = nextPoint ? [nextPoint.x, nextPoint.y] as [number, number] : undefined;
  const angleRad = cur && nextPt && (nextPt[0] !== cur[0] || nextPt[1] !== cur[1])
    ? Math.atan2(nextPt[1] - cur[1], nextPt[0] - cur[0])
    : -Math.PI / 2;
  const angleDeg = (angleRad * 180) / Math.PI + 90;

  const curT01Point = pointAtTime(targetTrack, currentTimeSec);
  const nextT01 = pointAtTime(targetTrack, currentTimeSec + Math.max(0.1, durationSec * 0.01));
  const curT01 = curT01Point ? [curT01Point.x, curT01Point.y] as [number, number] : undefined;
  const nextT01Pt = nextT01 ? [nextT01.x, nextT01.y] as [number, number] : undefined;
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
          aria-label="Replay time"
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
