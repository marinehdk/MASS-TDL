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
  info: 'var(--c-info)', warn: 'var(--c-warn)', crit: 'var(--c-danger)',
};

export const TimelineSixLane: React.FC<TimelineSixLaneProps> = ({
  events, durationSec, currentTimeSec, onScrub,
}) => {
  const trackRef = useRef<HTMLDivElement>(null);
  const [hoverTime, setHoverTime] = useState<number | null>(null);

  const handleClick = (e: React.MouseEvent) => {
    if (!trackRef.current) return;
    const rect = trackRef.current.getBoundingClientRect();
    const frac = (e.clientX - rect.left) / rect.width;
    onScrub(Math.round(frac * durationSec));
  };

  const handleMouseMove = (e: React.MouseEvent) => {
    if (!trackRef.current) return;
    const rect = trackRef.current.getBoundingClientRect();
    const frac = (e.clientX - rect.left) / rect.width;
    setHoverTime(Math.round(frac * durationSec));
  };

  const progressPct = durationSec > 0 ? (currentTimeSec / durationSec) * 100 : 0;

  const formatTime = (t: number) => {
    const m = Math.floor(t / 60).toString().padStart(2, '0');
    const s = Math.floor(t % 60).toString().padStart(2, '0');
    return `T+${m}:${s}`;
  };

  return (
    <div data-testid="timeline-6lane" style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
      {/* Timeline header */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <span style={{
          fontFamily: 'var(--f-disp)', fontSize: 9, color: 'var(--txt-3)',
          letterSpacing: '0.16em', textTransform: 'uppercase',
        }}>
          EVENT TIMELINE
        </span>
        <span style={{ fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--txt-2)' }}>
          {events.length} events &middot; click to scrub
        </span>
      </div>

      {/* Track */}
      <div
        ref={trackRef}
        data-testid="timeline-playback"
        onClick={handleClick}
        onMouseMove={handleMouseMove}
        onMouseLeave={() => setHoverTime(null)}
        style={{
          position: 'relative', height: 28,
          background: 'var(--bg-2)', borderRadius: 3,
          cursor: 'pointer', overflow: 'hidden',
          border: '1px solid var(--line-1)',
        }}
      >
        {/* Event segments */}
        {events.map((evt, i) => {
          const leftPct = (evt.t / durationSec) * 100;
          const color = SEV_COLORS[evt.sev] ?? 'var(--txt-3)';
          return (
            <div key={i} title={`${formatTime(evt.t)} &mdash; ${evt.k}: ${evt.d}`} style={{
              position: 'absolute', left: `${leftPct}%`, top: 0, bottom: 0,
              width: 3, background: color,
              opacity: 0.8,
            }} />
          );
        })}

        {/* Progress bar */}
        <div style={{
          position: 'absolute', left: 0, top: 0, bottom: 0,
          width: `${progressPct}%`,
          background: 'rgba(91,192,190,0.12)',
          borderRight: '2px solid var(--c-phos)',
          pointerEvents: 'none',
        }} />

        {/* Time ticks */}
        {[0, 0.25, 0.5, 0.75, 1].map((frac) => (
          <div key={frac} style={{
            position: 'absolute', left: `${frac * 100}%`, bottom: 0,
            width: 1, height: 6, background: 'var(--line-2)',
          }}>
            <div style={{
              position: 'absolute', bottom: 8, left: '50%', transform: 'translateX(-50%)',
              fontFamily: 'var(--f-mono)', fontSize: 7, color: 'var(--txt-3)',
              whiteSpace: 'nowrap',
            }}>
              {formatTime(Math.round(frac * durationSec))}
            </div>
          </div>
        ))}
      </div>

      {/* Hover tooltip */}
      {hoverTime != null && (
        <div style={{
          fontFamily: 'var(--f-mono)', fontSize: 9, color: 'var(--c-phos)',
          textAlign: 'center',
        }}>
          {formatTime(hoverTime)}
        </div>
      )}
    </div>
  );
};
