import { describe, it, expect, vi, beforeEach } from 'vitest';
import { fireEvent, render, screen } from '@testing-library/react';
import { SimulationEvaluator } from '../SimulationEvaluator';
import { useScenarioStore } from '../../store';

const apiMocks = vi.hoisted(() => ({
  getEvidenceLibrarySessions: vi.fn(),
  getEvidenceReplay: vi.fn(),
  getDecisionFrame: vi.fn(),
  getLastRunScoring: vi.fn(),
}));

vi.mock('../../api/silApi', () => ({
  useExportMarzipMutation: () => [vi.fn().mockResolvedValue({ status: 'processing' }), { isLoading: false }],
  useGetExportStatusQuery: vi.fn(() => ({ data: null })),
  useGetEvidenceLibrarySessionsQuery: apiMocks.getEvidenceLibrarySessions,
  useRescanEvidenceLibraryMutation: () => [vi.fn().mockResolvedValue({ ingested: 0 }), { isLoading: false }],
  useGetEvidenceReplayQuery: apiMocks.getEvidenceReplay,
  useGetDecisionFrameQuery: apiMocks.getDecisionFrame,
  useGetLastRunScoringQuery: apiMocks.getLastRunScoring,
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

const emptyEvidenceLibrary = () => ({
  data: { sessions: [] },
  isLoading: false,
  refetch: vi.fn(),
});

const replayDetail = () => ({
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
  });

const evidenceLibraryWithScenario = (scenarioId = 'colreg-rule14-ho') => ({
  data: {
    sessions: [{
      evidence_id: 'evidence-123',
      session_id: 'session-123',
      source: 'cli',
      suite: 'single',
      root_id: 'local',
      session_path: '/tmp/session',
      valid_data: true,
      scenario_count: 1,
      scenario_ids: [scenarioId],
      ingest_status: 'indexed',
    }],
  },
  isLoading: false,
  refetch: vi.fn(),
});

const scoring = () => ({
    data: {
      run_id: 'test-run-id-12345',
      scenario_id: 'stale-scoring-scenario',
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
  });

beforeEach(() => {
  useScenarioStore.getState().reset();
  apiMocks.getEvidenceLibrarySessions.mockReturnValue(emptyEvidenceLibrary());
  apiMocks.getEvidenceReplay.mockReturnValue(replayDetail());
  apiMocks.getDecisionFrame.mockReturnValue({ data: null, isLoading: false });
  apiMocks.getLastRunScoring.mockReturnValue(scoring());
  vi.clearAllMocks();
});

describe('SimulationEvaluator', () => {
  it('renders replay detail successfully with indexed replay data', () => {
    apiMocks.getEvidenceLibrarySessions.mockReturnValue(evidenceLibraryWithScenario());
    render(<SimulationEvaluator evidenceId="evidence-123" />);
    expect(screen.getByText('session-123')).toBeInTheDocument();
    expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
    expect(screen.getByTestId('trajectory-replay')).toBeInTheDocument();
    expect(screen.getByTestId('timeline-6lane')).toBeInTheDocument();
  });

  it('renders evidence library when no evidence id is bound', () => {
    render(<SimulationEvaluator />);
    expect(screen.getByText('Evidence Library')).toBeInTheDocument();
  });

  it('navigates from evidence library to selected replay', () => {
    apiMocks.getEvidenceLibrarySessions.mockReturnValue(evidenceLibraryWithScenario());
    window.location.hash = '#/evaluator';

    render(<SimulationEvaluator />);
    fireEvent.click(screen.getByText('Open Replay'));

    expect(window.location.hash).toBe('#/evaluator/evidence-123');
  });

  it('renders replay detail when evidence id is bound', () => {
    apiMocks.getEvidenceLibrarySessions.mockReturnValue(evidenceLibraryWithScenario());
    render(<SimulationEvaluator evidenceId="evidence-123" />);
    expect(screen.getByTestId('trajectory-replay')).toBeInTheDocument();
    expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
  });

  it('resolves latest route to the newest indexed evidence session', () => {
    apiMocks.getEvidenceLibrarySessions.mockReturnValue(evidenceLibraryWithScenario('latest-scenario'));

    render(<SimulationEvaluator evidenceId="latest" />);

    expect(apiMocks.getEvidenceReplay).toHaveBeenCalledWith(
      { evidenceId: 'evidence-123', scenarioId: 'latest-scenario' },
    );
    expect(screen.getByTestId('trajectory-replay')).toBeInTheDocument();
  });

  it('resolves replay scenario from indexed evidence session metadata', () => {
    apiMocks.getEvidenceLibrarySessions.mockReturnValue(evidenceLibraryWithScenario('indexed-scenario'));

    render(<SimulationEvaluator evidenceId="evidence-123" />);

    expect(apiMocks.getEvidenceReplay).toHaveBeenCalledWith(
      { evidenceId: 'evidence-123', scenarioId: 'indexed-scenario' },
    );
    expect(apiMocks.getDecisionFrame).not.toHaveBeenCalled();
  });

  it('skips replay queries until evidence id and indexed scenario id are known', () => {
    render(<SimulationEvaluator evidenceId="evidence-123" />);

    expect(apiMocks.getEvidenceReplay).not.toHaveBeenCalledWith(
      expect.objectContaining({ evidenceId: 'evidence-123' }),
    );
    expect(apiMocks.getDecisionFrame).not.toHaveBeenCalledWith(
      expect.objectContaining({ evidenceId: 'evidence-123' }),
    );
  });

  it('ignores stale store evidence id when route evidence id is absent', () => {
    useScenarioStore.getState().setEvidenceId('stale-evidence');

    render(<SimulationEvaluator />);

    expect(screen.getByText('Evidence Library')).toBeInTheDocument();
    expect(apiMocks.getEvidenceReplay).not.toHaveBeenCalledWith(
      expect.objectContaining({ evidenceId: 'stale-evidence' }),
    );
  });
});
