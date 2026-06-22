import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
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
      {
        id: 'sil-nodes',
        service: 'sil-nodes',
        class: 'core_service',
        container_name: 'mass-l3-sil-sil-nodes-1',
        status: 'running',
        health: 'healthy',
        image: 'mass-l3-sil-nodes:local',
        allowed_actions: ['restart'],
      },
      {
        id: 'foxglove-bridge',
        service: 'foxglove-bridge',
        class: 'core_service',
        container_name: 'mass-l3-sil-foxglove-bridge-1',
        status: 'running',
        health: 'healthy',
        image: 'mass-l3-sil-foxglove-bridge:local',
        allowed_actions: ['restart'],
      },
      {
        id: 'martin-tile-server',
        service: 'martin-tile-server',
        class: 'core_service',
        container_name: 'mass-l3-sil-martin-tile-server-1',
        status: 'running',
        health: 'healthy',
        image: 'ghcr.io/maplibre/martin:latest',
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
      {
        role: 'hydrodynamics',
        active_plugin: 'hydro-fossen',
        single_instance: true,
        plugins: [
          {
            id: 'hydro-fossen',
            label: 'Hydro Fossen',
            service: 'plugin-hydro-fossen',
            container: 'mass-l3-plugin-hydro-1',
            status: 'running',
            health: 'healthy',
            image: 'mass-hydro-fossen:0.9.3',
            expected_image: 'mass-hydro-fossen:0.9.3',
            revision: 'local',
            revision_label: 'org.opencontainers.image.revision',
            required_topics: { '/ship/odometry': 'nav_msgs/msg/Odometry', '/ship/waypoints': 'nav_msgs/msg/Path' },
            topic_status: 'ok',
            health_required: true,
            ros_domain_id: 10,
          },
        ],
      },
      {
        role: 'fusion',
        active_plugin: 'yougc-fusion',
        single_instance: true,
        plugins: [
          {
            id: 'yougc-fusion',
            label: 'YouGC Fusion',
            service: 'plugin-fusion-yougc',
            container: 'mass-l3-plugin-fusion-1',
            status: 'running',
            health: 'healthy',
            image: 'yougc-fusion:20260612',
            expected_image: 'yougc-fusion:20260612',
            revision: 'local',
            revision_label: 'org.opencontainers.image.revision',
            required_topics: {
              '/fusion/tracked_targets': 'nmea_interfaces/msg/TrackedTargetArray',
              '/gps/fix': 'nmea_interfaces/msg/Gps',
              '/heading': 'nmea_interfaces/msg/Heading',
            },
            topic_status: 'ok',
            health_required: true,
            ros_domain_id: 11,
          },
        ],
      },
    ],
    gates: [
      {
        name: 'core_services_running',
        passed: true,
        services: {
          'sil-orchestrator': 'running',
          'sil-nodes': 'running',
          'foxglove-bridge': 'running',
          'martin-tile-server': 'running',
        },
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
          {
            role: 'hydrodynamics',
            active_plugin: 'hydro-fossen',
            running_plugins: ['hydro-fossen'],
            passed: true,
          },
          {
            role: 'fusion',
            active_plugin: 'yougc-fusion',
            running_plugins: ['yougc-fusion'],
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
  startEvidenceSession: vi.fn(),
  finalizeEvidenceSession: vi.fn(),
  telemetryReset: vi.fn(),
  updateLifecycleStatus: vi.fn(),
  controlReset: vi.fn(),
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
  useTelemetryStore: { getState: () => ({ reset: mocks.telemetryReset, updateLifecycleStatus: mocks.updateLifecycleStatus }) },
  useControlStore: { getState: () => ({ reset: mocks.controlReset }) },
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
  useStartEvidenceSessionMutation: () => [mocks.startEvidenceSession],
  useFinalizeEvidenceSessionMutation: () => [mocks.finalizeEvidenceSession],
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
vi.mock('../shared/LiveLogStream', () => ({
  LiveLogStream: () => <div data-testid="preflight-livelog" />,
}));

import { SimulationCheck } from '../SimulationCheck';

describe('SimulationCheck runtime console', () => {
  beforeEach(() => {
    window.location.hash = '#/check/safe_route';
    mocks.verdict = null;
    mocks.runtimeSummary = {
      ...mocks.runtimeSummary,
      mode: 'internal',
      target: 'local',
      active_profile: 'internal-local',
      verdict: 'IDLE',
    };
    mocks.gates = [
      {
        gate_id: 1,
        passed: true,
        duration_ms: 1133.8,
        started_at: '2026-06-15T00:00:00Z',
        finished_at: '2026-06-15T00:00:01Z',
        evidence: {},
      },
      {
        gate_id: 2,
        label: '模块脉搏健康',
        passed: true,
        duration_ms: 1007.7,
        started_at: '2026-06-15T00:00:01Z',
        finished_at: '2026-06-15T00:00:02Z',
        evidence: {},
      },
      {
        gate_id: 4,
        label: 'ODD-场景一致',
        passed: true,
        duration_ms: 7.1,
        started_at: '2026-06-15T00:00:02Z',
        finished_at: '2026-06-15T00:00:03Z',
        evidence: {},
      },
      {
        gate_id: 5,
        label: '时基严密性验证',
        passed: true,
        duration_ms: 7.1,
        started_at: '2026-06-15T00:00:03Z',
        finished_at: '2026-06-15T00:00:04Z',
        evidence: {},
      },
      {
        gate_id: 6,
        label: '架构物理隔离',
        passed: true,
        duration_ms: 7.1,
        started_at: '2026-06-15T00:00:04Z',
        finished_at: '2026-06-15T00:00:05Z',
        evidence: {},
      },
    ];
    mocks.streaming = false;
    mocks.error = '';
    mocks.telemetryReset.mockReset();
    mocks.updateLifecycleStatus.mockReset();
    mocks.controlReset.mockReset();
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
    mocks.stopRuntimeCoreStack.mockReturnValue({
      unwrap: () => Promise.resolve({ accepted: true }),
    });
    mocks.switchRuntimePlugin.mockReset();
    mocks.probeRuntime.mockReset();
    mocks.probeRuntime.mockReturnValue({
      unwrap: () => Promise.resolve({ ...mocks.runtimeSummary, verdict: 'GO' }),
    });
    mocks.startEvidenceSession.mockReset();
    mocks.startEvidenceSession.mockReturnValue({
      unwrap: () => Promise.resolve({ session_id: '20260622_153012_frontend_safe_route' }),
    });
    mocks.finalizeEvidenceSession.mockReset();
    mocks.finalizeEvidenceSession.mockReturnValue({
      unwrap: () => Promise.resolve({ discarded: false }),
    });
  });

  it('renders runtime console controls on Screen 02', () => {
    render(<SimulationCheck />);

    expect(screen.getByText('仿真检查 · 容器运行台')).toBeInTheDocument();
    expect(screen.getByText('安全门控总览')).toBeInTheDocument();
    expect(screen.getByText('检查点 01')).toBeInTheDocument();
    expect(screen.getAllByText('运行模式确认').length).toBeGreaterThan(0);
    expect(screen.getAllByText('内测模式').length).toBeGreaterThan(0);
    expect(screen.getAllByText('本地MOCK，没有集成其他容器').length).toBeGreaterThan(0);
    expect(screen.getByText('检查点 02')).toBeInTheDocument();
    expect(screen.getByText('内部核心容器')).toBeInTheDocument();
    expect(screen.getByText('4/4 核心容器')).toBeInTheDocument();
    expect(screen.getByText('4个核心容器检查是否通过')).toBeInTheDocument();
    expect(screen.getByText('检查点 03')).toBeInTheDocument();
    expect(screen.getByText('外部核心容器')).toBeInTheDocument();
    expect(screen.getByText('0/0 角色容器')).toBeInTheDocument();
    expect(screen.getByText('内测模式不启用外部角色容器')).toBeInTheDocument();
    expect(screen.getByText('检查点 04')).toBeInTheDocument();
    expect(screen.getByText('ROS2数据链路')).toBeInTheDocument();
    expect(screen.getAllByText('6/6 所有话题数量')).toHaveLength(1);
    expect(screen.getByText('/sil/own_ship_state')).toBeInTheDocument();
    expect(screen.getByText('/sil/module_pulse')).toBeInTheDocument();
    expect(screen.queryByText('/fusion/tracked_targets')).not.toBeInTheDocument();
    expect(screen.queryByText('/ship/odometry')).not.toBeInTheDocument();
    expect(screen.getByText('检查点 05')).toBeInTheDocument();
    expect(screen.getByText('安全边界检查')).toBeInTheDocument();
    expect(screen.getByText('ODD-场景一致')).toBeInTheDocument();
    expect(screen.getByText('时基严密性验证')).toBeInTheDocument();
    expect(screen.getByText('架构物理隔离')).toBeInTheDocument();
    expect(screen.getByText('检查点 06')).toBeInTheDocument();
    expect(screen.getByText('仿真检查结论')).toBeInTheDocument();
    expect(screen.getByText('FIX')).toBeInTheDocument();
    expect(screen.getByText('通过检查或者完成修复')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '内测' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '集成' })).toBeInTheDocument();
    expect(screen.getByText('当前模式：内测模式')).toBeInTheDocument();
    expect(screen.getByText('默认选中')).toBeInTheDocument();
    expect(screen.getByText('内部核心：4')).toBeInTheDocument();
    expect(screen.getByText('外部插件：0')).toBeInTheDocument();
    expect(screen.getByText('检查结论：检查中')).toBeInTheDocument();
    expect(screen.queryByText('No runtime evidence')).not.toBeInTheDocument();
    expect(screen.queryByText('TDL 核心容器状态')).not.toBeInTheDocument();
    expect(screen.queryByText('外部插件容器状态')).not.toBeInTheDocument();
    expect(screen.queryByTestId('gate-sequencer')).not.toBeInTheDocument();
    expect(screen.queryByTestId('external-integration-panel')).not.toBeInTheDocument();
  });

  it('keeps all left-rail checkpoints passed in internal display mode when backend plugin gates are not applicable', () => {
    mocks.verdict = 'GO';
    mocks.runtimeSummary = {
      ...mocks.runtimeSummary,
      mode: 'integration',
      active_profile: 'integration-local',
      verdict: 'NO-GO',
      gates: [
        {
          name: 'core_services_running',
          passed: true,
          services: {
            'sil-orchestrator': 'running',
            'sil-nodes': 'running',
            'foxglove-bridge': 'running',
            'martin-tile-server': 'running',
          },
        },
        {
          name: 'single_active_plugin_per_role',
          passed: false,
          roles: [],
        },
      ],
    };

    render(<SimulationCheck />);

    const leftRail = screen.getByTestId('check-category-nav');
    expect(within(leftRail).getByRole('button', { name: /外部核心容器/i })).toHaveTextContent('通过');
    expect(within(leftRail).getByRole('button', { name: /外部核心容器/i })).toHaveTextContent('0/0 角色容器');
    expect(within(leftRail).getByRole('button', { name: /仿真检查结论/i })).toHaveTextContent('通过');
    expect(screen.getByText('检查结论：通过')).toBeInTheDocument();
  });

  it('places the final GO action in the bottom button row', () => {
    mocks.verdict = 'GO';

    render(<SimulationCheck />);

    const leftRail = screen.getByTestId('check-category-nav');
    const centerDecision = screen.getByTestId('center-decision-panel');

    expect(within(leftRail).queryByText('决策结论')).not.toBeInTheDocument();
    expect(within(centerDecision).getByRole('button', { name: '人工确认 GO' })).toBeEnabled();
  });

  it('uses the selected display mode in the left rail instead of the backend mode', () => {
    mocks.runtimeSummary = {
      ...mocks.runtimeSummary,
      mode: 'integration',
      active_profile: 'integration-local',
    };

    render(<SimulationCheck />);

    const leftRail = screen.getByTestId('check-category-nav');
    expect(within(leftRail).getByText('内测模式')).toBeInTheDocument();
    expect(within(leftRail).getByText('本地MOCK，没有集成其他容器')).toBeInTheDocument();
    expect(within(leftRail).queryByText('集成模式')).not.toBeInTheDocument();
    expect(within(leftRail).queryByText('真实容器，集成除L3外发布版本')).not.toBeInTheDocument();
  });

  it('uses a two-column layout and merges logs into the middle column', () => {
    render(<SimulationCheck />);

    expect(screen.getByTestId('preflight')).toHaveStyle({ gridTemplateColumns: '300px minmax(0, 1fr)' });
    expect(screen.queryByTestId('right-decision-panel')).not.toBeInTheDocument();
    expect(screen.getByTestId('runtime-bottom-console')).toBeInTheDocument();
    expect(screen.getByTestId('center-decision-panel')).toBeInTheDocument();
  });

  it('pins the bottom console to the lower half with one log frame and equal bottom buttons', () => {
    mocks.verdict = 'GO';

    render(<SimulationCheck />);

    expect(screen.getByTestId('runtime-main')).toHaveStyle({
      gridTemplateRows: 'minmax(0, 1fr) minmax(0, 1fr)',
    });
    expect(screen.queryByTestId('action-logs')).not.toBeInTheDocument();
    expect(screen.queryByText('Runtime Actions')).not.toBeInTheDocument();
    expect(screen.getByTestId('runtime-log-frame')).toBeInTheDocument();
    expect(screen.getByTestId('runtime-bottom-actions')).toHaveStyle({
      gridTemplateColumns: 'repeat(3, minmax(0, 1fr))',
    });

    const bottomActions = screen.getByTestId('runtime-bottom-actions');
    expect(within(bottomActions).getByRole('button', { name: '重新检查' })).toBeInTheDocument();
    expect(within(bottomActions).getByRole('button', { name: '返回场景' })).toBeInTheDocument();
    expect(within(bottomActions).getByRole('button', { name: '人工确认 GO' })).toBeEnabled();
  });

  it('shows only the selected runtime module in the middle column', () => {
    render(<SimulationCheck />);

    expect(screen.getByTestId('runtime-module-mode')).toBeInTheDocument();
    expect(screen.queryByTestId('runtime-module-core')).not.toBeInTheDocument();
    expect(screen.queryByTestId('runtime-module-plugins')).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: /内部核心容器/i }));

    expect(screen.queryByTestId('runtime-module-mode')).not.toBeInTheDocument();
    expect(screen.getByTestId('runtime-module-core')).toBeInTheDocument();
    expect(screen.queryByTestId('runtime-module-plugins')).not.toBeInTheDocument();
  });

  it('shows core services as compact 2x2 cards with real restart actions', async () => {
    render(<SimulationCheck />);

    fireEvent.click(screen.getByRole('button', { name: /内部核心容器/i }));

    const coreGrid = screen.getByTestId('runtime-core-grid');
    expect(coreGrid).toHaveStyle({ gridTemplateColumns: 'repeat(2, minmax(0, 1fr))' });
    expect(screen.getAllByTestId(/^core-service-card-/)).toHaveLength(4);
    expect(screen.queryByRole('button', { name: 'Stop Core Stack' })).not.toBeInTheDocument();

    const orchestratorCard = screen.getByTestId('core-service-card-sil-orchestrator');
    fireEvent.click(within(orchestratorCard).getByRole('button', { name: '重启' }));

    await waitFor(() => {
      expect(mocks.restartRuntimeCoreService).toHaveBeenCalledWith('sil-orchestrator');
    });
  });

  it('shows only internal ROS2 topic cards in internal mode', () => {
    render(<SimulationCheck />);

    fireEvent.click(screen.getByRole('button', { name: /ROS2数据链路/i }));

    const rosGrid = screen.getByTestId('runtime-ros-grid');
    expect(rosGrid).toHaveStyle({ gridTemplateColumns: 'repeat(3, minmax(0, 1fr))' });
    expect(screen.getAllByTestId(/^ros-topic-card-/).length).toBeGreaterThan(0);
    expect(screen.getAllByText('/sil/own_ship_state').length).toBeGreaterThan(0);
    expect(screen.getByText('/sil/actuator_cmd')).toBeInTheDocument();
    expect(screen.getAllByText(/联通/).length).toBeGreaterThan(0);
    expect(screen.getAllByText(/数据/).length).toBeGreaterThan(0);
    expect(screen.queryByText('/fusion/tracked_targets')).not.toBeInTheDocument();
    expect(screen.queryByText('/route_planning/route_plan')).not.toBeInTheDocument();
  });

  it('shows external plugin topic cards when integration mode is selected', () => {
    render(<SimulationCheck />);

    fireEvent.click(screen.getByRole('button', { name: '集成' }));
    fireEvent.click(screen.getByRole('button', { name: /ROS2数据链路/i }));

    expect(screen.getAllByText('/fusion/tracked_targets').length).toBeGreaterThan(0);
    expect(screen.getAllByText('/ship/odometry').length).toBeGreaterThan(0);
    expect(screen.getAllByText('/l2/planned_route').length).toBeGreaterThan(0);
  });

  it('shows safety boundary checks as compact cards', () => {
    render(<SimulationCheck />);

    fireEvent.click(screen.getByRole('button', { name: /安全边界检查/i }));

    const safetyGrid = screen.getByTestId('runtime-safety-grid');
    expect(safetyGrid).toHaveStyle({ gridTemplateColumns: 'repeat(3, minmax(0, 1fr))' });
    expect(screen.getAllByTestId(/^safety-check-card-/).length).toBeGreaterThanOrEqual(3);
    expect(screen.getAllByText('ODD-场景一致').length).toBeGreaterThan(0);
    expect(screen.getAllByText('时基严密性验证').length).toBeGreaterThan(0);
    expect(screen.getAllByText('架构物理隔离').length).toBeGreaterThan(0);
  });

  it('defaults the mode switch to internal and updates the top plugin count when integration is selected', () => {
    mocks.runtimeSummary = {
      ...mocks.runtimeSummary,
      mode: 'integration',
      active_profile: 'integration-local',
    };

    render(<SimulationCheck />);

    const internal = screen.getByRole('button', { name: '内测' });
    const integration = screen.getByRole('button', { name: '集成' });

    expect(internal).toHaveAttribute('aria-pressed', 'true');
    expect(integration).toHaveAttribute('aria-pressed', 'false');

    fireEvent.click(integration);

    expect(internal).toHaveAttribute('aria-pressed', 'false');
    expect(integration).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByText('当前模式：集成模式')).toBeInTheDocument();
    expect(screen.getByText('外部插件：3')).toBeInTheDocument();
  });

  it('does not auto-launch Screen 03 when preflight reaches GO', async () => {
    vi.useFakeTimers();
    try {
      mocks.verdict = 'GO';

      render(<SimulationCheck />);
      await act(async () => {
        await vi.advanceTimersByTimeAsync(3100);
      });
    } finally {
      vi.useRealTimers();
    }

    expect(mocks.probeRuntime).not.toHaveBeenCalled();
    expect(mocks.configureLifecycle).not.toHaveBeenCalled();
    expect(window.location.hash).toBe('#/check/safe_route');
  });

  it('blocks lifecycle activation after explicit GO when runtime gate probe fails', async () => {
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
    fireEvent.click(screen.getByRole('button', { name: '人工确认 GO' }));

    await waitFor(() => expect(mocks.probeRuntime).toHaveBeenCalledTimes(1));
    expect(mocks.configureLifecycle).not.toHaveBeenCalled();
    expect(screen.getByText(/Runtime gate failed/)).toBeInTheDocument();
  });

  it('launches Screen 03 only after explicit GO confirmation passes runtime probe', async () => {
    mocks.verdict = 'GO';

    render(<SimulationCheck />);
    fireEvent.click(screen.getByRole('button', { name: '人工确认 GO' }));

    await waitFor(() => expect(mocks.probeRuntime).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(mocks.configureLifecycle).toHaveBeenCalledWith('safe_route'));
    expect(mocks.cleanupLifecycle).not.toHaveBeenCalled();
    expect(mocks.activateLifecycle).toHaveBeenCalled();
    expect(mocks.startEvidenceSession).toHaveBeenCalledWith({
      source: 'frontend',
      suite: 'frontend',
      scenario_id: 'safe_route',
    });
    expect(mocks.updateLifecycleStatus).toHaveBeenCalledWith({
      scenario_id: 'safe_route',
      current_state: 3,
      sim_time: 0,
    });
    expect(window.location.hash).toBe('#/monitor/safe_route');
  });

  it('finalizes frontend evidence session when configure fails after explicit GO', async () => {
    mocks.verdict = 'GO';
    mocks.configureLifecycle.mockReturnValue({
      unwrap: () => Promise.resolve({ success: false, error: 'bad config' }),
    });

    render(<SimulationCheck />);
    fireEvent.click(screen.getByRole('button', { name: '人工确认 GO' }));

    await waitFor(() => expect(mocks.startEvidenceSession).toHaveBeenCalledWith({
      source: 'frontend',
      suite: 'frontend',
      scenario_id: 'safe_route',
    }));
    await waitFor(() => expect(mocks.finalizeEvidenceSession).toHaveBeenCalledWith({
      sessionId: '20260622_153012_frontend_safe_route',
      scenario_id: 'safe_route',
      status: 'error',
    }));
    expect(mocks.activateLifecycle).not.toHaveBeenCalled();
  });
});
