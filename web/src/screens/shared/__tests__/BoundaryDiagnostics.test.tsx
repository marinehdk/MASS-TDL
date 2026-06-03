import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import React from 'react';
import { BoundaryDiagnostics } from '../BoundaryDiagnostics';
import type { TimelineEvent } from '../TimelineSixLane';

describe('BoundaryDiagnostics', () => {
  it('renders a PASS status when boundaries are not breached', () => {
    render(
      <BoundaryDiagnostics
        minCpaNm={0.5}
        maxRudderDeg={15.0}
        events={[]}
      />
    );

    expect(screen.getByTestId('boundary-diagnostics')).toBeInTheDocument();
    expect(
      screen.getByText(/✓ SAFETY & BOUNDARY STATUS: PASS/i)
    ).toBeInTheDocument();
    expect(
      screen.getByText(/No parameter boundary violations detected/i)
    ).toBeInTheDocument();
    expect(screen.queryByTestId('boundary-warning-0')).toBeNull();
  });

  it('renders CPA late-avoidance advice when CPA is breached and latency > 10s', () => {
    const events: TimelineEvent[] = [
      { t: 10.0, k: 'CPA_PROJ', sev: 'warn', m: 'M2', d: 'Projected CPA < 0.27' },
      { t: 25.5, k: 'MPC_BRANCH', sev: 'info', m: 'M5', d: 'Avoidance branch selected' },
    ];

    render(
      <BoundaryDiagnostics
        minCpaNm={0.2}
        maxRudderDeg={20.0}
        events={events}
      />
    );

    const container = screen.getByTestId('boundary-diagnostics');
    expect(container).toBeInTheDocument();
    
    // Header should show warning state
    expect(screen.getByText('AUTOMATED BOUNDARY DIAGNOSTICS')).toBeInTheDocument();
    
    // Check advice text
    const warning = screen.getByText(
      /Avoidance planner took action late \(ΔT = 15\.5s\)\. Suggest increasing 'mso_cpa_threshold_nm' parameter/i
    );
    expect(warning).toBeInTheDocument();
  });

  it('renders CPA prompt-avoidance advice when CPA is breached and latency <= 10s', () => {
    const events: TimelineEvent[] = [
      { t: 10.0, k: 'CPA_PROJ', sev: 'warn', m: 'M2', d: 'Projected CPA < 0.27' },
      { t: 15.2, k: 'MPC_BRANCH', sev: 'info', m: 'M5', d: 'Avoidance branch selected' },
    ];

    render(
      <BoundaryDiagnostics
        minCpaNm={0.2}
        maxRudderDeg={20.0}
        events={events}
      />
    );

    const warning = screen.getByText(
      /Avoidance planner reacted promptly \(ΔT = 5\.2s\), but the maneuver angle was insufficient\. Suggest increasing the collision avoidance safety domain buffer 'safety_domain_starboard_nm'/i
    );
    expect(warning).toBeInTheDocument();
  });

  it('renders CPA missing events advice when events are missing', () => {
    render(
      <BoundaryDiagnostics
        minCpaNm={0.2}
        maxRudderDeg={20.0}
        events={[]}
      />
    );

    const warning = screen.getByText(
      /Min CPA threshold breached\. Suggest increasing avoidance domain sizes \('safety_domain_starboard_nm'\)\./i
    );
    expect(warning).toBeInTheDocument();
  });

  it('renders rudder limit warning when rudder exceeds 35°', () => {
    render(
      <BoundaryDiagnostics
        minCpaNm={0.5}
        maxRudderDeg={36.5}
        events={[]}
      />
    );

    const warning = screen.getByText(
      /Hard rudder limit exceeded \(> 35\.0°\)\. Suggest smoothing control command filter rates or adjusting the yaw rate penalty 'weight_yaw_rate'/i
    );
    expect(warning).toBeInTheDocument();
  });

  it('renders both CPA and Rudder warnings when both are breached', () => {
    const events: TimelineEvent[] = [
      { t: 10.0, k: 'CPA_PROJ', sev: 'warn', m: 'M2', d: 'Projected CPA < 0.27' },
      { t: 22.0, k: 'MPC_BRANCH', sev: 'info', m: 'M5', d: 'Avoidance branch selected' },
    ];

    render(
      <BoundaryDiagnostics
        minCpaNm={0.2}
        maxRudderDeg={40.0}
        events={events}
      />
    );

    expect(screen.getByText(/Avoidance planner took action late/i)).toBeInTheDocument();
    expect(screen.getByText(/Hard rudder limit exceeded/i)).toBeInTheDocument();
    
    expect(screen.queryByText(/✓ SAFETY & BOUNDARY STATUS: PASS/i)).toBeNull();
  });
});
