import React, { useRef, useState } from 'react';

export interface TimelineEvent {
  t: number;          // sim time seconds
  k: string;          // event type key
  sev: 'info' | 'warn' | 'crit' | string;
  m: string;          // module
  d: string;          // description
}

interface TimelineSixLaneProps {
  events: TimelineEvent[];
  durationSec: number;
  currentTimeSec: number;
  onScrub: (timeSec: number) => void;
}

const SEV_COLORS: Record<string, string> = {
  info: 'var(--c-info)',
  warn: 'var(--c-warn)',
  crit: 'var(--c-danger)',
};

const LANES = [
  { label: 'OWN', name: 'Ownship Status' },
  { label: 'TGT', name: 'Target Vessel' },
  { label: 'M4',  name: 'M4 COLREGs' },
  { label: 'M5',  name: 'M5 Avoidance' },
  { label: 'M7',  name: 'M7 Safety' },
  { label: 'HUM', name: 'Human Interface' },
];

const getLaneIndex = (evt: TimelineEvent): number => {
  const m = evt.m || (evt as any).module || '';
  const k = evt.k || (evt as any).type || '';
  if (k === 'ToR_REQ' || k === 'ToR_ACK' || k === 'OVERRIDE' || k === 'HANDBACK') return 5; // HUMAN
  if (m === 'M7' || k.includes('VETO') || k.includes('HB_LOSS')) return 4; // M7
  if (m === 'M5' || k.includes('MPC') || k.includes('Avoid')) return 3; // M5
  if (m === 'M6' || k.includes('COLREG') || k.includes('Rule') || k.includes('R14') || k.includes('SCENE_CHG')) return 2; // M4/M6
  if (m === 'M2' || k.includes('T01') || k.includes('AIS') || k.includes('CPA')) return 1; // Target
  return 0; // Ownship
};

export const TimelineSixLane: React.FC<TimelineSixLaneProps> = ({
  events, durationSec, currentTimeSec, onScrub,
}) => {
  const trackRef = useRef<HTMLDivElement>(null);
  const [hoverTime, setHoverTime] = useState<number | null>(null);

  const handleScrub = (clientX: number) => {
    if (!trackRef.current) return;
    const rect = trackRef.current.getBoundingClientRect();
    const frac = (clientX - rect.left) / rect.width;
    const time = Math.min(durationSec, Math.max(0, Math.round(frac * durationSec)));
    onScrub(time);
  };

  const handleClick = (e: React.MouseEvent) => {
    handleScrub(e.clientX);
  };

  const handleMouseMove = (e: React.MouseEvent) => {
    if (!trackRef.current) return;
    const rect = trackRef.current.getBoundingClientRect();
    const frac = (e.clientX - rect.left) / rect.width;
    setHoverTime(Math.min(durationSec, Math.max(0, Math.round(frac * durationSec))));
    if (e.buttons === 1) { // dragging
      handleScrub(e.clientX);
    }
  };

  const progressPct = durationSec > 0 ? (currentTimeSec / durationSec) * 100 : 0;

  const formatTime = (t: number) => {
    const m = Math.floor(t / 60).toString().padStart(2, '0');
    const s = Math.floor(t % 60).toString().padStart(2, '0');
    return `T+${m}:${s}`;
  };

  return (
    <div data-testid="timeline-6lane" style={{ display: 'flex', flexDirection: 'column', gap: 4, height: '100%', padding: '8px 12px', background: 'var(--bg-1)', border: '1px solid var(--line-2)' }}>
      {/* Timeline header */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <span style={{
            fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-1)',
            letterSpacing: '0.16em', textTransform: 'uppercase', fontWeight: 600,
          }}>
            EVENT TIMELINE (6-LANE AUDIT)
          </span>
          <span style={{
            fontFamily: 'var(--f-mono)', fontSize: 8, color: 'var(--c-warn)',
            border: '1px solid var(--c-warn)', borderRadius: 2, padding: '0 4px',
            letterSpacing: '0.05em', background: 'rgba(230,140,0,0.1)'
          }}>
            [DEMO DATA - MOCK ACTIVE]
          </span>
        </div>
        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-3)' }}>
          {formatTime(currentTimeSec)} / {formatTime(durationSec)} &middot; drag to scrub
        </span>
      </div>

      {/* 6 Lanes Track Container */}
      <div
        ref={trackRef}
        data-testid="timeline-playback"
        onClick={handleClick}
        onMouseMove={handleMouseMove}
        onMouseLeave={() => setHoverTime(null)}
        style={{
          position: 'relative', flex: 1,
          display: 'flex', flexDirection: 'column',
          background: '#070C13', borderRadius: 4,
          cursor: 'ew-resize', overflow: 'hidden',
          border: '1px solid var(--line-2)',
          padding: '4px 0',
        }}
      >
        {/* Time Ticks background guidelines */}
        {[0.25, 0.5, 0.75].map((frac) => (
          <div key={frac} style={{
            position: 'absolute', left: `${frac * 100}%`, top: 0, bottom: 0,
            width: 1, borderLeft: '1px dashed var(--line-3)', opacity: 0.3,
            pointerEvents: 'none',
          }} />
        ))}

        {/* 6 lanes rendering */}
        {LANES.map((lane, laneIdx) => {
          // Get events for this lane
          const laneEvents = events.filter((e) => getLaneIndex(e) === laneIdx);
          
          return (
            <div key={laneIdx} style={{
              flex: 1, position: 'relative', display: 'flex', alignItems: 'center',
              borderBottom: laneIdx < 5 ? '1px solid rgba(255,255,255,0.03)' : 'none',
            }}>
              {/* Lane Label */}
              <div style={{
                position: 'absolute', left: 4, top: '50%', transform: 'translateY(-50%)',
                fontFamily: 'var(--f-mono)', fontSize: 8, color: 'var(--txt-3)',
                background: 'rgba(7,12,19,0.8)', padding: '1px 3px', borderRadius: 2,
                zIndex: 10, pointerEvents: 'none', border: '1px solid var(--line-3)',
              }} title={lane.name}>
                {lane.label}
              </div>

              {/* Lane Centerline */}
              <div style={{
                position: 'absolute', left: 0, right: 0, height: 1,
                borderBottom: '1px dashed rgba(255,255,255,0.08)',
                pointerEvents: 'none',
              }} />

              {/* Event nodes on this lane */}
              {laneEvents.map((evt, i) => {
                const leftPct = (evt.t / durationSec) * 100;
                const k = evt.k || (evt as any).type || '';
                const sev = evt.sev || (evt as any).payload?.severity?.toLowerCase() || 'info';
                const color = SEV_COLORS[sev] ?? 'var(--txt-2)';
                const size = sev === 'crit' ? 8 : sev === 'warn' ? 7 : 6;
                const isCurrent = Math.abs(evt.t - currentTimeSec) < 5;
                const desc = evt.d || (typeof (evt as any).payload === 'object' ? JSON.stringify((evt as any).payload) : String((evt as any).payload || ''));
                
                return (
                  <div
                    key={i}
                    title={`${formatTime(evt.t)} [${k}]: ${desc}`}
                    style={{
                      position: 'absolute',
                      left: `${leftPct}%`,
                      transform: 'translateX(-50%)',
                      width: size,
                      height: size,
                      borderRadius: '50%',
                      background: color,
                      border: isCurrent ? '1px solid #fff' : `1px solid rgba(0,0,0,0.5)`,
                      boxShadow: sev === 'crit' ? '0 0 6px var(--c-danger)' : sev === 'warn' ? '0 0 4px var(--c-warn)' : 'none',
                      zIndex: isCurrent ? 5 : 2,
                    }}
                  />
                );
              })}
            </div>
          );
        })}

        {/* Scrubber vertical line indicator */}
        <div style={{
          position: 'absolute', left: `${progressPct}%`, top: 0, bottom: 0,
          width: 1.5, background: 'var(--c-phos)',
          boxShadow: '0 0 6px var(--c-phos)',
          pointerEvents: 'none', zIndex: 15,
        }} />

        {/* Hover vertical line guide */}
        {hoverTime != null && (
          <div style={{
            position: 'absolute', left: `${(hoverTime / durationSec) * 100}%`, top: 0, bottom: 0,
            width: 1, borderLeft: '1px dashed var(--c-phos)', opacity: 0.5,
            pointerEvents: 'none', zIndex: 14,
          }} />
        )}
      </div>

      {/* Time axis footer ticks */}
      <div style={{ display: 'flex', justifyContent: 'space-between', padding: '0 4px', pointerEvents: 'none' }}>
        {[0, 0.25, 0.5, 0.75, 1].map((frac) => (
          <span key={frac} style={{ fontFamily: 'var(--f-mono)', fontSize: 7.5, color: 'var(--txt-3)' }}>
            {formatTime(Math.round(frac * durationSec))}
          </span>
        ))}
      </div>

      {/* Hover tooltip text */}
      {hoverTime != null && (
        <div style={{
          fontFamily: 'var(--f-mono)', fontSize: 8, color: 'var(--c-phos)',
          textAlign: 'center', marginTop: -2, height: 8
        }}>
          Scrub to: {formatTime(hoverTime)}
        </div>
      )}
    </div>
  );
};
