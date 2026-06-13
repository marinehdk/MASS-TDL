import { act, render, screen, waitFor } from '@testing-library/react';
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
  runtimeSummary: {
    mode: 'internal',
    target: 'local',
    active_profile: 'internal-local',
    verdict: 'IDLE',
    evidence_path: 'runs/runtime_probe.json',
    core_services: [
      {
        id: 'sil-orchestrator',
        service: 'sil-orchestrator',
        class: 'core_service',
        container_name: 'mass-l3-sil-sil-orchestrator-1',
        status: 'running',
        health: 'healthy',
        image: 'mass-l3-sil-orchestrator:local',
        allowed_actions: ['restart'],
      },
    ],
    plugin_roles: [
      {
        role: 'route_l2',
        active_plugin: 'tdl-mock-route',
        single_instance: true,
        plugins: [
          {
            id: 'tdl-mock-route',
            label: 'TDL Mock Route',
            service: 'plugin-route-tdl-mock',
            container: 'mass-l3-plugin-route-1',
            status: 'running',
            health: 'healthy',
            image: 'mass-l3-plugin-route-mock:local',
            expected_image: 'mass-l3-plugin-route-mock:local',
            revision: 'local',
            revision_label: 'org.opencontainers.image.revision',
            required_topics: { '/l2/planned_route': 'nav_msgs/msg/Path' },
            topic_status: 'ok',
            health_required: false,
            ros_domain_id: 42,
          },
        ],
      },
    ],
    gates: [
      {
        name: 'core_services_running',
        passed: true,
        services: { 'sil-orchestrator': 'running' },
      },
      {
        name: 'single_active_plugin_per_role',
        passed: true,
        roles: [
          {
            role: 'route_l2',
            active_plugin: 'tdl-mock-route',
            running_plugins: ['tdl-mock-route'],
            passed: true,
          },
        ],
      },
    ],
  } as any,
  refetchRuntimeSummary: vi.fn(),
  restartRuntimeCoreService: vi.fn(),
  stopRuntimeCoreStack: vi.fn(),
  switchRuntimePlugin: vi.fn(),
  probeRuntime: vi.fn(),
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
  useGetRuntimeSummaryQuery: () => ({ data: mocks.runtimeSummary, refetch: mocks.refetchRuntimeSummary }),
  useRestartRuntimeCoreServiceMutation: () => [mocks.restartRuntimeCoreService],
  useStopRuntimeCoreStackMutation: () => [mocks.stopRuntimeCoreStack],
  useSwitchRuntimePluginMutation: () => [mocks.switchRuntimePlugin],
  useProbeRuntimeMutation: () => [mocks.probeRuntime],
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

describe('SimulationCheck runtime console', () => {
  beforeEach(() => {
    window.location.hash = '#/check/safe_route';
    mocks.verdict = null;
    mocks.gates = [];
    mocks.streaming = false;
    mocks.error = '';
    mocks.configureLifecycle.mockReset();
    mocks.configureLifecycle.mockReturnValue({
      unwrap: () => Promise.resolve({ success: true }),
    });
    mocks.activateLifecycle.mockReset();
    mocks.activateLifecycle.mockReturnValue({
      unwrap: () => Promise.resolve({ success: true }),
    });
    mocks.cleanupLifecycle.mockReset();
    mocks.cleanupLifecycle.mockResolvedValue({ success: true });
    mocks.refetchRuntimeSummary.mockReset();
    mocks.restartRuntimeCoreService.mockReset();
    mocks.stopRuntimeCoreStack.mockReset();
    mocks.switchRuntimePlugin.mockReset();
    mocks.probeRuntime.mockReset();
    mocks.probeRuntime.mockReturnValue({
      unwrap: () => Promise.resolve({ ...mocks.runtimeSummary, verdict: 'GO' }),
    });
  });

  it('renders runtime console controls on Screen 02', () => {
    render(<SimulationCheck />);

    expect(screen.getByText('仿真检查 · 容器运行台')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '内测' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '集成' })).toBeInTheDocument();
    expect(screen.getByText('TDL 核心容器')).toBeInTheDocument();
    expect(screen.getByText('外部插件容器')).toBeInTheDocument();
    expect(screen.queryByTestId('external-integration-panel')).not.toBeInTheDocument();
  });

  it('blocks lifecycle activation when runtime gate probe fails', async () => {
    vi.useFakeTimers();
    try {
      mocks.verdict = 'GO';
      mocks.probeRuntime.mockReturnValue({
        unwrap: () =>
          Promise.resolve({
            ...mocks.runtimeSummary,
            verdict: 'NO-GO',
            gates: [
              {
                name: 'core_services_running',
                passed: false,
                services: { 'sil-orchestrator': 'stopped' },
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

    await waitFor(() => expect(mocks.probeRuntime).toHaveBeenCalled());
    expect(mocks.configureLifecycle).not.toHaveBeenCalled();
    expect(screen.getByText(/Runtime gate failed/)).toBeInTheDocument();
  });
});
