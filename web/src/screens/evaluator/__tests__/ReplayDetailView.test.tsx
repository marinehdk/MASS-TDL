import { describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { ReplayDetailView } from '../ReplayDetailView';

const apiMocks = vi.hoisted(() => ({
  useGetDecisionFrameQuery: vi.fn(() => ({
    data: {
      evidence_id: 'ev-1',
      scenario_id: 'colreg-rule14-ho',
      sim_t: 0,
      chain: {
        M2: { status: 'OK', status_source: 'diagnostic_availability', facts: { primary_target_id: 'T01' } },
        M6: { status: 'WARN', status_source: 'diagnostic_availability', facts: { rule: 'Rule14', role: 'give_way' } },
        M5: { status: 'OK', status_source: 'diagnostic_availability', facts: { solver_status: 'VALID' } },
      },
      gates: [
        { gate_id: 'G-SEM', status: 'FAIL', temporal_scope: 'final_run_verdict', payload_json: '{}', source: 'TraceEvaluationReport' },
      ],
      nearby_events: [
        { event_id: 1, sim_t: 5, module: 'M5', event_type: 'PLAN_READY', severity: 'info', payload_json: '{"detail":"ready"}' },
      ],
    },
    isLoading: false,
  })),
}));

vi.mock('../../../api/silApi', () => ({
  useGetEvidenceReplayQuery: () => ({
    data: {
      session: { evidence_id: 'ev-1', session_id: 'session-1' },
      scenario: { scenario_id: 'colreg-rule14-ho', verdict: 'FAIL', overall_pass: false, min_cpa_nm: 0.243 },
      duration_s: 10,
      trajectory: [
        { vessel_id: 'OS-9', vessel_role: 'ownship', sim_t: 0, lat: 0, lon: 0, heading_deg: 0, sog_kn: 8 },
        { vessel_id: 'OS-9', vessel_role: 'ownship', sim_t: 10, lat: 0.01, lon: 0, heading_deg: 10, sog_kn: 8 },
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
  useGetDecisionFrameQuery: apiMocks.useGetDecisionFrameQuery,
}));

describe('ReplayDetailView', () => {
  afterEach(() => {
    apiMocks.useGetDecisionFrameQuery.mockClear();
  });

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

  it('opens chain inspector from failed gate at current replay time', () => {
    render(<ReplayDetailView evidenceId="ev-1" scenarioId="colreg-rule14-ho" />);

    fireEvent.change(screen.getByLabelText('Replay time'), { target: { value: '5' } });
    fireEvent.click(screen.getByText('G-SEM'));

    expect(screen.getByRole('button', { name: 'Close' })).toBeInTheDocument();
    expect(screen.getByText('M6')).toBeInTheDocument();
    expect(screen.getByText('Rule14')).toBeInTheDocument();
    expect(apiMocks.useGetDecisionFrameQuery).toHaveBeenLastCalledWith({
      evidenceId: 'ev-1',
      scenarioId: 'colreg-rule14-ho',
      simT: 5,
    });
  });

  it('opens chain inspector from timeline event and keeps event scrub behavior', () => {
    render(<ReplayDetailView evidenceId="ev-1" scenarioId="colreg-rule14-ho" />);

    fireEvent.click(screen.getByTitle(/PLAN_READY/));

    expect(screen.getAllByText('T+00:05').length).toBeGreaterThan(0);
    expect(screen.getByRole('button', { name: 'Close' })).toBeInTheDocument();
    expect(screen.getAllByText('M5').length).toBeGreaterThan(0);
    expect(screen.getByText('VALID')).toBeInTheDocument();
    expect(apiMocks.useGetDecisionFrameQuery).toHaveBeenLastCalledWith({
      evidenceId: 'ev-1',
      scenarioId: 'colreg-rule14-ho',
      simT: 5,
    });
  });
});
