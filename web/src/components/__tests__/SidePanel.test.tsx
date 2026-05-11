import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { SidePanel } from '../SidePanel';

describe('SidePanel', () => {
  it('renders standby state with null data', () => {
    render(<SidePanel data={null} />);
    expect(screen.getByText('Standby')).toBeTruthy();
    expect(screen.getByText('Awaiting run')).toBeTruthy();
  });

  it('renders live data with CPA/TCPA/Rule/Decision/ASDR', () => {
    render(<SidePanel data={{
      timestamp: '2026-05-11T00:00:00Z',
      scenario_id: 'r14-ho',
      sat1: null,
      sat2_reasoning: null,
      sat3_tdl_s: 184,
      sat3_tmr_s: 60,
      odd: { zone: 'A', envelope_state: 'IN', conformance_score: 0.87, confidence: 0.9 },
      job_status: 'running',
      cpa_nm: 0.18,
      tcpa_s: 520,
      rule_text: 'R14 Head-on',
      decision_text: 'M5 BC-MPC: right turn 10°',
      module_status: [true, true, false, true, false, true, false, false],
      asdr_events: [{
        timestamp: '14:32:01', event_type: 'step', step: 0, rule: 'R14',
        cpa_nm: 0.08, tcpa_s: 580, action: 'detecting', verdict: 'RISK',
      }],
    }} />);
    expect(screen.getByText('0.18')).toBeTruthy();
    expect(screen.getByText('520')).toBeTruthy();
    expect(screen.getByText('R14 Head-on')).toBeTruthy();
    expect(screen.getByText('RISK')).toBeTruthy();
  });
});
