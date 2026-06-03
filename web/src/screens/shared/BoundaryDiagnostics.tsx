import React from 'react';
import { LucideAlertTriangle, LucideCheckCircle2, LucideInfo } from 'lucide-react';
import type { TimelineEvent } from './TimelineSixLane';

interface BoundaryDiagnosticsProps {
  minCpaNm?: number;
  maxRudderDeg?: number;
  events: TimelineEvent[];
}

export const BoundaryDiagnostics: React.FC<BoundaryDiagnosticsProps> = ({
  minCpaNm,
  maxRudderDeg,
  events,
}) => {
  const warnings: string[] = [];

  // 1. Check if CPA is breached
  const isCpaBreached = minCpaNm != null && minCpaNm < 0.27;
  if (isCpaBreached) {
    const cpaProj = events.find((e) => e.k === 'CPA_PROJ');
    const mpcBranch = events.find((e) => e.k === 'MPC_BRANCH');

    if (cpaProj && mpcBranch) {
      const deltaT = mpcBranch.t - cpaProj.t;
      if (deltaT > 10) {
        warnings.push(
          `Avoidance planner took action late (ΔT = ${deltaT.toFixed(1)}s). Suggest increasing 'mso_cpa_threshold_nm' parameter to initiate COLREG assessment earlier.`
        );
      } else {
        warnings.push(
          `Avoidance planner reacted promptly (ΔT = ${deltaT.toFixed(1)}s), but the maneuver angle was insufficient. Suggest increasing the collision avoidance safety domain buffer 'safety_domain_starboard_nm' or penalization weight inside the MPC planner.`
        );
      }
    } else {
      warnings.push(
        "Min CPA threshold breached. Suggest increasing avoidance domain sizes ('safety_domain_starboard_nm')."
      );
    }
  }

  // 2. Check if Rudder is breached
  const isRudderBreached = maxRudderDeg != null && maxRudderDeg > 35.0;
  if (isRudderBreached) {
    warnings.push(
      "Hard rudder limit exceeded (> 35.0°). Suggest smoothing control command filter rates or adjusting the yaw rate penalty 'weight_yaw_rate' in the planner config."
    );
  }

  const hasBreach = warnings.length > 0;

  return (
    <div
      data-testid="boundary-diagnostics"
      style={{
        border: `1px solid ${hasBreach ? 'var(--c-danger)' : 'var(--c-stbd)'}`,
        borderLeft: `4px solid ${hasBreach ? 'var(--c-danger)' : 'var(--c-stbd)'}`,
        background: hasBreach ? 'rgba(220, 53, 69, 0.08)' : 'rgba(40, 167, 69, 0.06)',
        borderRadius: 4,
        padding: '16px',
        display: 'flex',
        flexDirection: 'column',
        gap: 12,
        fontFamily: 'var(--f-sans, sans-serif)',
        color: 'var(--txt-0)',
        boxShadow: '0 2px 8px rgba(0,0,0,0.15)',
        transition: 'all 0.2s ease-in-out',
      }}
    >
      {/* Header */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        {hasBreach ? (
          <LucideAlertTriangle size={20} color="var(--c-danger)" />
        ) : (
          <LucideCheckCircle2 size={20} color="var(--c-stbd)" />
        )}
        <span
          style={{
            fontFamily: 'var(--f-disp)',
            fontSize: 13,
            fontWeight: 600,
            letterSpacing: '0.08em',
            textTransform: 'uppercase',
            color: hasBreach ? 'var(--c-danger)' : 'var(--c-stbd)',
          }}
        >
          {hasBreach ? 'AUTOMATED BOUNDARY DIAGNOSTICS' : 'SAFETY & BOUNDARY STATUS: PASS'}
        </span>
      </div>

      {/* Main Content */}
      {hasBreach ? (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
          {warnings.map((warning, index) => (
            <div
              key={index}
              data-testid={`boundary-warning-${index}`}
              style={{
                display: 'flex',
                gap: 8,
                background: 'rgba(0, 0, 0, 0.2)',
                border: '1px solid rgba(255, 255, 255, 0.05)',
                borderRadius: 4,
                padding: '10px 12px',
                fontSize: 12,
                lineHeight: 1.5,
              }}
            >
              <LucideInfo
                size={16}
                color="var(--c-warn)"
                style={{ flexShrink: 0, marginTop: 1 }}
              />
              <div style={{ fontFamily: 'var(--f-mono)', color: 'var(--txt-1)' }}>
                {warning}
              </div>
            </div>
          ))}
        </div>
      ) : (
        <div
          style={{
            fontFamily: 'var(--f-mono)',
            fontSize: 12,
            color: 'var(--txt-1)',
            padding: '4px 0',
          }}
        >
          ✓ SAFETY & BOUNDARY STATUS: PASS. No parameter boundary violations detected. Envelopes nominal.
        </div>
      )}
    </div>
  );
};
