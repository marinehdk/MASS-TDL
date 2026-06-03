import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen } from '@testing-library/react';
import { SimulationEvaluator } from '../SimulationEvaluator';
import { useScenarioStore } from '../../store';

vi.mock('../../api/silApi', () => ({
  useExportMarzipMutation: () => [vi.fn().mockResolvedValue({ status: 'processing' }), { isLoading: false }],
  useGetExportStatusQuery: vi.fn(() => ({ data: null })),
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
});
