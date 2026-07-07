import { describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { ReplayDetailView } from '../ReplayDetailView';

vi.mock('../../../api/silApi', () => ({
  useGetEvidenceReplayQuery: () => ({
    data: {
      session: { evidence_id: 'ev-1', session_id: 'session-1' },
      scenario: { scenario_id: 'colreg-rule14-ho', verdict: 'FAIL', overall_pass: false, min_cpa_nm: 0.243 },
      duration_s: 10,
      trajectory: [
        { vessel_id: 'OWN', vessel_role: 'ownship', sim_t: 0, lat: 0, lon: 0, heading_deg: 0, sog_kn: 8 },
        { vessel_id: 'OWN', vessel_role: 'ownship', sim_t: 10, lat: 0.01, lon: 0, heading_deg: 10, sog_kn: 8 },
        { vessel_id: 'T01', vessel_role: 'target', sim_t: 0, lat: 0.01, lon: 0.01, heading_deg: 180, sog_kn: 8 },
      ],
      events: [
        { event_id: 1, sim_t: 5, module: 'M5', event_type: 'PLAN_READY', severity: 'info', payload_json: '{"detail":"ready"}' },
      ],
      gates: [
        { gate_id: 'G-SEM', status: 'FAIL', temporal_scope: 'final_run_verdict', payload_json: '{}', source: 'TraceEvaluationReport' },
      ],
      artifacts: [],
    },
    isLoading: false,
  }),
  useGetDecisionFrameQuery: () => ({ data: null, isLoading: false }),
}));

describe('ReplayDetailView', () => {
  it('renders indexed replay data and scrubs from controls and timeline events', () => {
    render(<ReplayDetailView evidenceId="ev-1" scenarioId="colreg-rule14-ho" />);

    expect(screen.getByText('session-1')).toBeInTheDocument();
    expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
    expect(screen.getByText('G-SEM')).toBeInTheDocument();

    fireEvent.change(screen.getByLabelText('Replay time'), { target: { value: '5' } });
    expect(screen.getAllByText('T+00:05').length).toBeGreaterThan(0);

    fireEvent.change(screen.getByLabelText('Replay time'), { target: { value: '0' } });
    fireEvent.click(screen.getByTitle(/PLAN_READY/));
    expect(screen.getAllByText('T+00:05').length).toBeGreaterThan(0);
  });
});
