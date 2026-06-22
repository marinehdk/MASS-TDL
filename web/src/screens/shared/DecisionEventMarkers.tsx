import React from 'react';
import type { DecisionTimelineEvent } from './avoidancePhase';

interface DecisionEventMarkersProps {
  events: DecisionTimelineEvent[];
  durationSec: number;
  onSelectTime?: (timeSec: number) => void;
}

const COLOR_BY_SEVERITY: Record<string, string> = {
  info: '#34d399',
  warn: '#fbbf24',
  crit: '#f87171',
};

const LABEL_BY_KIND: Record<string, string> = {
  M6_RULE_ASSERTED: 'M6!',
  M4_MANEUVER_START: 'M4>',
  M5_PLAN_READY: 'M5>',
  M7_SAFETY_ALERT: 'M7!',
  CLEAR_RETURN: 'CLR',
};

export const DecisionEventMarkers: React.FC<DecisionEventMarkersProps> = ({
  events,
  durationSec,
  onSelectTime,
}) => {
  if (events.length === 0 || durationSec <= 0) return null;

  return (
    <div
      data-testid="decision-event-markers"
      style={{
        position: 'absolute',
        inset: '0 0 0 0',
        pointerEvents: 'none',
      }}
    >
      {events.map((event, index) => {
        const pct = Math.max(0, Math.min(100, (event.t / durationSec) * 100));
        const color = COLOR_BY_SEVERITY[event.sev] ?? COLOR_BY_SEVERITY.info;
        const label = LABEL_BY_KIND[event.k] ?? event.m;
        return (
          <button
            key={`${event.k}-${event.t}-${index}`}
            type="button"
            data-testid={`decision-event-${event.k}`}
            data-severity={event.sev}
            title={`${label} ${event.d}`}
            onClick={() => onSelectTime?.(event.t)}
            style={{
              position: 'absolute',
              left: `${pct}%`,
              top: '50%',
              transform: 'translate(-50%, -50%)',
              minWidth: 22,
              height: 16,
              borderRadius: 4,
              border: `1px solid ${color}`,
              background: 'rgba(7,12,19,0.96)',
              color,
              fontFamily: 'var(--f-mono)',
              fontSize: 8,
              fontWeight: 900,
              lineHeight: '14px',
              padding: '0 3px',
              pointerEvents: 'auto',
              cursor: onSelectTime ? 'pointer' : 'default',
              boxShadow: `0 0 8px ${color}66`,
            }}
          >
            {label}
          </button>
        );
      })}
    </div>
  );
};
