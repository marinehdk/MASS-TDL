import { act, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { beforeEach, describe, expect, it, vi } from 'vitest';

const mocks = vi.hoisted(() => ({
  gates: [] as any[],
  verdict: null as any,
  streaming: false,
  error: '',
  start: vi.fn(),
  abort: vi.fn(),
  configureLifecycle: vi.fn(),
  activateLifecycle: vi.fn(),
  cleanupLifecycle: vi.fn(),
  profiles: {
    active_profile: 'default',
    profiles: ['a4000_external', 'default'] as any[],
  },
  status: {
    active_profile: 'default',
    external_enabled: false,
    route_out_enabled: false,
  },
  probe: vi.fn(),
  fetchIntegrationStatus: vi.fn(),
  selectProfile: vi.fn(),
  refetchProfiles: vi.fn(),
  refetchStatus: vi.fn(),
}));

vi.mock('../../hooks/useGateStream', () => ({
  useGateStream: () => ({
    gates: mocks.gates,
    verdict: mocks.verdict,
    streaming: mocks.streaming,
    error: mocks.error,
    start: mocks.start,
    abort: mocks.abort,
  }),
}));

vi.mock('../../hooks/useHotkeys', () => ({ useHotkeys: vi.fn() }));

vi.mock('../../store', () => ({
  useScenarioStore: () => ({ runId: 'run-test' }),
  useTelemetryStore: { getState: () => ({ reset: vi.fn() }) },
  useControlStore: { getState: () => ({ reset: vi.fn() }) },
}));

vi.mock('../../api/silApi', () => ({
  useGetScenarioQuery: () => ({ data: { yaml_content: 'title: test' } }),
  useConfigureLifecycleMutation: () => [mocks.configureLifecycle],
  useActivateLifecycleMutation: () => [mocks.activateLifecycle],
  useCleanupLifecycleMutation: () => [mocks.cleanupLifecycle],
  useSkipPreflightMutation: () => [vi.fn()],
  useListIntegrationProfilesQuery: () => ({ data: mocks.profiles, refetch: mocks.refetchProfiles }),
  useGetIntegrationStatusQuery: () => ({ data: mocks.status, refetch: mocks.refetchStatus }),
  useLazyGetIntegrationStatusQuery: () => [mocks.fetchIntegrationStatus],
  useSelectIntegrationProfileMutation: () => [mocks.selectProfile],
  useProbeIntegrationMutation: () => [mocks.probe],
}));

vi.mock('../shared/GateSequencer', () => ({
  GateSequencer: () => <div data-testid="gate-sequencer" />,
}));
vi.mock('../shared/DiagnosticCanvas', () => ({
  DiagnosticCanvas: () => <div data-testid="diagnostic-canvas" />,
}));
vi.mock('../shared/ActionLogs', () => ({
  ActionLogs: () => <div data-testid="action-logs" />,
}));

import { SimulationCheck } from '../SimulationCheck';

describe('SimulationCheck external integration panel', () => {
  beforeEach(() => {
    window.location.hash = '#/check/safe_route';
    mocks.probe.mockReset();
    mocks.selectProfile.mockReset();
    mocks.configureLifecycle.mockReset();
    mocks.activateLifecycle.mockReset();
    mocks.cleanupLifecycle.mockReset();
    mocks.fetchIntegrationStatus.mockReset();
    mocks.refetchProfiles.mockReset();
    mocks.refetchStatus.mockReset();
    mocks.verdict = null;
    mocks.status = {
      active_profile: 'default',
      external_enabled: false,
      route_out_enabled: false,
    };
    mocks.fetchIntegrationStatus.mockReturnValue({
      unwrap: () => Promise.resolve(mocks.status),
    });
  });

  it('renders external integration panel on Screen 02', () => {
    render(<SimulationCheck />);

    expect(screen.getByTestId('external-integration-panel')).toBeInTheDocument();
    expect(screen.getByText('External Integration')).toBeInTheDocument();
    expect(screen.getAllByText('default').length).toBeGreaterThan(0);
  });

  it('selects a4000 profile from Screen 02', async () => {
    mocks.selectProfile.mockReturnValue({
      unwrap: () => Promise.resolve({ name: 'a4000_external' }),
    });

    render(<SimulationCheck />);
    fireEvent.change(screen.getByTestId('integration-profile-select'), {
      target: { value: 'a4000_external' },
    });

    await waitFor(() => {
      expect(mocks.selectProfile).toHaveBeenCalledWith({ name: 'a4000_external' });
      expect(mocks.refetchProfiles).toHaveBeenCalled();
      expect(mocks.refetchStatus).toHaveBeenCalled();
    });
  });

  it('runs integration probe from panel button', async () => {
    mocks.probe.mockReturnValue({
      unwrap: () => Promise.resolve({ all_clear: true, checks: [] }),
    });

    render(<SimulationCheck />);
    fireEvent.click(screen.getByTestId('integration-probe-button'));

    await waitFor(() => expect(mocks.probe).toHaveBeenCalled());
  });

  it('blocks lifecycle activation when external gate probe fails', async () => {
    vi.useFakeTimers();
    try {
      mocks.verdict = 'GO';
      mocks.status = {
        active_profile: 'a4000_external',
        external_enabled: true,
        route_out_enabled: true,
      };
      mocks.probe.mockReturnValue({
        unwrap: () =>
          Promise.resolve({
            profile_name: 'a4000_external',
            all_clear: false,
            checks: [
              {
                gate_id: 108,
                label: 'Low-level control forbidden',
                passed: false,
                detail: 'failed',
              },
            ],
          }),
      });

      render(<SimulationCheck />);
      await act(async () => {
        await vi.advanceTimersByTimeAsync(3100);
      });
    } finally {
      vi.useRealTimers();
    }

    await waitFor(() => expect(mocks.probe).toHaveBeenCalled());
    expect(mocks.configureLifecycle).not.toHaveBeenCalled();
    expect(screen.getByText(/External integration gate failed/)).toBeInTheDocument();
  });

  it('uses fresh integration status before GO even when cached status is default', async () => {
    vi.useFakeTimers();
    try {
      mocks.verdict = 'GO';
      mocks.status = {
        active_profile: 'default',
        external_enabled: false,
        route_out_enabled: false,
      };
      mocks.fetchIntegrationStatus.mockReturnValue({
        unwrap: () =>
          Promise.resolve({
            active_profile: 'a4000_external',
            external_enabled: true,
            route_out_enabled: true,
          }),
      });
      mocks.probe.mockReturnValue({
        unwrap: () =>
          Promise.resolve({
            profile_name: 'a4000_external',
            all_clear: false,
            checks: [
              {
                gate_id: 102,
                label: 'Topic type match',
                passed: false,
                detail: 'failed',
              },
            ],
          }),
      });

      render(<SimulationCheck />);
      await act(async () => {
        await vi.advanceTimersByTimeAsync(3100);
      });
    } finally {
      vi.useRealTimers();
    }

    await waitFor(() => expect(mocks.fetchIntegrationStatus).toHaveBeenCalled());
    expect(mocks.probe).toHaveBeenCalled();
    expect(mocks.configureLifecycle).not.toHaveBeenCalled();
    expect(screen.getByText(/External integration gate failed: Topic type match/)).toBeInTheDocument();
  });
});
