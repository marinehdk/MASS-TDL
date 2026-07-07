import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { SimulationEvaluator } from '../SimulationEvaluator';
import { useScenarioStore } from '../../store';

vi.mock('../../api/silApi', () => ({
  useExportMarzipMutation: () => [vi.fn().mockResolvedValue({ status: 'processing' }), { isLoading: false }],
  useGetExportStatusQuery: vi.fn(() => ({ data: null })),
  useGetEvidenceLibrarySessionsQuery: vi.fn(() => ({
    data: { sessions: [] },
    isLoading: false,
    refetch: vi.fn(),
  })),
  useRescanEvidenceLibraryMutation: () => [vi.fn().mockResolvedValue({ ingested: 0 }), { isLoading: false }],
  useGetEvidenceReplayQuery: vi.fn(() => ({
    data: {
      session: { evidence_id: 'evidence-123', session_id: 'session-123' },
      scenario: { scenario_id: 'colreg-rule14-ho', verdict: 'PASS', overall_pass: true },
      duration_s: 10,
      trajectory: [
        { vessel_id: 'OWN', vessel_role: 'ownship', sim_t: 0, lat: 0, lon: 0, heading_deg: 0, sog_kn: 8 },
        { vessel_id: 'OWN', vessel_role: 'ownship', sim_t: 10, lat: 0.01, lon: 0, heading_deg: 0, sog_kn: 8 },
      ],
      events: [],
      gates: [],
      artifacts: [],
    },
    isLoading: false,
  })),
  useGetDecisionFrameQuery: vi.fn(() => ({ data: null, isLoading: false })),
  useGetLastRunScoringQuery: vi.fn(() => ({
    data: {
      run_id: 'test-run-id-12345',
      verdict: 'pass',
      kpis: {
        min_cpa_nm: 0.35,
        tcpa_min_s: 120,
        avg_rot_dpm: 5.2,
        max_rudder_deg: 25.0,
        grounding_risk_score: 0.95,
        route_deviation_nm: 0.12,
        time_to_mrm_s: 0,
      },
      scoring_dimensions: {
        safety: 1.0,
        rule_compliance: 1.0,
        delay_penalty: 0.0,
        action_magnitude_penalty: 0.0,
        phase_score: 1.0,
        plausibility: 1.0,
        total: 1.0,
      },
      rule_chain: [],
    },
    isLoading: false,
    refetch: vi.fn(),
  })),
  useGetAsdrEventsQuery: vi.fn(() => ({
    data: {
      events: [
        {
          t: 47.0,
          type: 'SCENE_CHG',
          module: 'M1',
          payload: { from: 'TRANSIT', to: 'COLREG_AVOIDANCE' },
        } as any
      ],
      ledger: [
        {
          time: 'T+00:00',
          type: 'INIT',
          module: 'M1',
          payload: { scene: 'TRANSIT', odd_status: 'NOMINAL' },
          hash: 'abcdef123456',
        } as any
      ],
    },
    isLoading: false,
  })),
}));

beforeEach(() => {
  useScenarioStore.getState().reset();
});

describe('SimulationEvaluator', () => {
  it('renders report container successfully with run_id and KPIs', () => {
    render(<SimulationEvaluator />);
    expect(screen.getByText('EXPORT MARZIP')).toBeInTheDocument();
    expect(screen.getByText('0.350 nm')).toBeInTheDocument();
    expect(screen.getByText('120 s')).toBeInTheDocument();
    expect(screen.getByTestId('decision-tree')).toBeInTheDocument();
    expect(screen.getByTestId('boundary-diagnostics')).toBeInTheDocument();
  });

  it('renders evidence library when no evidence id is bound', () => {
    render(<SimulationEvaluator />);
    expect(screen.getByText('Evidence Library')).toBeInTheDocument();
  });

  it('renders replay detail when evidence id is bound', () => {
    render(<SimulationEvaluator evidenceId="evidence-123" />);
    expect(screen.getByTestId('trajectory-replay')).toBeInTheDocument();
    expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
  });
});
