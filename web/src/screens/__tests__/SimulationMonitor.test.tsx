import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';

const mockContainer = document.createElement('div');
Object.defineProperty(mockContainer, 'clientHeight', { value: 800 });

const mockMap = {
  addControl: vi.fn(), remove: vi.fn(),
  on: vi.fn(), off: vi.fn(),
  addSource: vi.fn(), addLayer: vi.fn(),
  getSource: vi.fn(() => ({ setData: vi.fn() })),
  getLayer: vi.fn(() => null),
  getCenter: vi.fn(() => ({ lng: 10.4, lat: 63.4 })),
  getContainer: vi.fn(() => mockContainer),
  getStyle: vi.fn(() => ({ layers: [{ id: 'base' }] })),
  jumpTo: vi.fn(), easeTo: vi.fn(), fitBounds: vi.fn(), setPadding: vi.fn(),
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  setPaintProperty: vi.fn(), setLayoutProperty: vi.fn(),
  project: vi.fn(() => ({ x: 100, y: 100 })),
};
vi.mock('maplibre-gl', () => ({
  default: {
    Map: vi.fn(() => mockMap),
    NavigationControl: vi.fn(() => ({})),
    ScaleControl: vi.fn(() => ({})),
    Marker: vi.fn(() => ({
      setLngLat: vi.fn().mockReturnThis(), addTo: vi.fn().mockReturnThis(),
      setRotation: vi.fn().mockReturnThis(), remove: vi.fn(),
      getElement: vi.fn(() => ({ innerHTML: '' })),
    })),
  },
}));

vi.mock('../../hooks/useFoxgloveLive', () => ({ useFoxgloveLive: vi.fn() }));
vi.mock('../../hooks/useHotkeys', () => ({
  useHotkeys: vi.fn((handlers: Record<string, () => void>) => {
    const handler = (e: KeyboardEvent) => {
      const key = e.key.toLowerCase();
      if (key === 'g') handlers.onToggleEngineer?.();
      if (key === 'v') handlers.onToggleRoc?.();
      if (key === ' ') handlers.onSpace?.();
      if (key === 't') handlers.onTor?.();
      if (key === 'f') handlers.onFault?.();
      if (key === 'm') handlers.onMrc?.();
      if (key === 'h') handlers.onHandback?.();
    };
    document.addEventListener('keydown', handler);
    return () => document.removeEventListener('keydown', handler);
  }),
}));
vi.mock('../../api/silApi', () => ({
  useDeactivateLifecycleMutation: () => [vi.fn().mockResolvedValue({})],
  useChangeLifecycleRateMutation: () => [vi.fn().mockResolvedValue({})],
  useInjectFaultMutation: () => [vi.fn().mockResolvedValue({})],
  useCancelFaultMutation: () => [vi.fn().mockResolvedValue({})],
  useInjectEncounterMutation: () => [vi.fn(() => ({ unwrap: () => Promise.resolve({ accepted: true, mmsi: 990000001 }) }))],
  useClearEncountersMutation: () => [vi.fn(() => ({ unwrap: () => Promise.resolve({ removed_count: 1 }) }))],
  useGetLifecycleStatusQuery: vi.fn(() => ({ data: null })),
  useGetScenarioQuery: vi.fn(() => ({ data: null, isLoading: false })),
}));

import { useGetLifecycleStatusQuery, useGetScenarioQuery } from '../../api/silApi';
import { SimulationMonitor } from '../SimulationMonitor';
import { useUIStore } from '../../store/uiStore';
import { useFsmStore } from '../../store/fsmStore';
import { useTelemetryStore } from '../../store';

beforeEach(() => {
  vi.useRealTimers();
  window.location.hash = '#/monitor';
  useUIStore.getState().reset();
  useUIStore.getState().setViewMode('captain');
  useFsmStore.setState({ currentState: 'TRANSIT', transitionHistory: [], torRequest: null });
  useTelemetryStore.setState({ wsConnected: true, ownShip: null } as any);
  vi.mocked(useGetLifecycleStatusQuery).mockReturnValue({ data: null } as any);
});

describe('SimulationMonitor', () => {
  it('renders monitor container', () => {
    render(<SimulationMonitor />);
    expect(screen.getByTestId('simulation-monitor')).toBeInTheDocument();
  });

  it('captain view is default', () => {
    render(<SimulationMonitor />);
    expect(useUIStore.getState().viewMode).toBe('captain');
  });

  it('G key switches to engineer view', () => {
    render(<SimulationMonitor />);
    fireEvent.keyDown(document, { key: 'g' });
    expect(useUIStore.getState().viewMode).toBe('engineer');
  });

  it('renders Captain and Monitor tab rails', () => {
    render(<SimulationMonitor />);
    expect(screen.getByTestId('left-tab-ship')).toBeInTheDocument();
    expect(screen.getByTestId('right-tab-asdr')).toBeInTheDocument();
  });

  it('offers only CPU-supported simulation rates', () => {
    render(<SimulationMonitor />);
    expect(screen.getByTestId('rate-btn-1x')).toBeInTheDocument();
    expect(screen.getByTestId('rate-btn-5x')).toBeInTheDocument();
    expect(screen.getByTestId('rate-btn-10x')).toBeInTheDocument();
    expect(screen.queryByTestId('rate-btn-50x')).not.toBeInTheDocument();
  });

  it('does not mount real AIS controls in the monitor screen', () => {
    render(<SimulationMonitor />);

    expect(screen.queryByTestId('ais-live-toggle')).not.toBeInTheDocument();
  });

  it('clicking captain tab toggles state and displays content panel', () => {
    render(<SimulationMonitor />);
    const shipTab = screen.getByTestId('left-tab-ship');
    
    // Collapsed by default, ship status details not rendered
    expect(screen.queryByText('航行状态')).not.toBeInTheDocument();
    
    // Click tab -> expands panel
    fireEvent.click(shipTab);
    expect(screen.getByText('航行状态')).toBeInTheDocument();
    
    // Click tab again -> collapses panel
    fireEvent.click(shipTab);
    expect(screen.queryByText('航行状态')).not.toBeInTheDocument();
  });

  it('MRC state applies blood-red border class', () => {
    useFsmStore.setState({ currentState: 'MRC', transitionHistory: [], torRequest: null });
    useUIStore.getState().setViewMode('captain');
    render(<SimulationMonitor />);
    const monitor = screen.getByTestId('simulation-monitor');
    expect(monitor.getAttribute('data-fsm')).toBe('MRC');
  });

  it('TOR state shows amber border indicator', () => {
    useFsmStore.setState({
      currentState: 'TOR',
      transitionHistory: [],
      torRequest: {
        reason: 'test', triggeredAtSimTime: 0, tmrDeadlineSimTime: 60,
        currentSituation: '', proposedAction: '',
      },
    });
    render(<SimulationMonitor />);
    const monitor = screen.getByTestId('simulation-monitor');
    expect(monitor.getAttribute('data-fsm')).toBe('TOR');
  });

  it('converts ownShip state values from radians/mps/radps to degrees/knots/deg-per-min', () => {
    useTelemetryStore.setState({
      wsConnected: true,
      ownShip: {
        pose: { lat: 63.4, lon: 10.4, heading: 0.5 },
        kinematics: { sog: 5, cog: 0.5, rot: 0.05, u: 5, v: 0, r: 0.05 },
        controlState: { rudderAngle: 0, throttle: 0 }
      }
    } as any);

    render(<SimulationMonitor />);
    const shipTab = screen.getByTestId('left-tab-ship');
    fireEvent.click(shipTab);

    // Expect heading converted to degrees: 0.5 rad -> 28.6°
    expect(screen.getByText('28.6°')).toBeInTheDocument();
    // Expect rudder angle: 0 rad -> 0.0°
    expect(screen.getByText('0.0°')).toBeInTheDocument();
    // Expect SOG converted to knots: 5 m/s -> 9.7 kn
    expect(screen.getByText('9.7 kn')).toBeInTheDocument();
    // Expect THR throttle level: 0 -> STOP
    expect(screen.getByText('STOP')).toBeInTheDocument();
  });

  it('converts ownShip state values with non-zero rudder and throttle', () => {
    useTelemetryStore.setState({
      wsConnected: true,
      ownShip: {
        pose: { lat: 63.4, lon: 10.4, heading: 0.5 },
        kinematics: { sog: 5, cog: 0.5, rot: 0.05, u: 5, v: 0, r: 0.05 },
        controlState: { rudderAngle: -0.087266, throttle: 0.5 } // -5° (L), AH 2
      }
    } as any);

    render(<SimulationMonitor />);
    const shipTab = screen.getByTestId('left-tab-ship');
    fireEvent.click(shipTab);

    // Expect rudder angle: -5° L
    expect(screen.getByText('5.0° L')).toBeInTheDocument();
    // Expect THR throttle level: AH 2
    expect(screen.getByText('AH 2')).toBeInTheDocument();
  });

  it('displays YAML 场景配置 as default voyage source and renders dynamic calculated data', () => {
    // Mock get scenario query to return nominalRoute mock config data
    const mockYaml = `
ownShip:
  nominalRoute:
    - latitude: 63.4
      longitude: 10.4
      target_sog_kn: 12.0
    - latitude: 63.5
      longitude: 10.4
      target_sog_kn: 12.0
`;
    vi.mocked(useGetScenarioQuery).mockReturnValue({
      data: { yaml_content: mockYaml },
      isLoading: false
    } as any);

    useTelemetryStore.setState({
      wsConnected: true,
      ownShip: {
        pose: { lat: 63.4, lon: 10.4, heading: 0.0 },
        kinematics: { sog: 5.0, cog: 0.0, rot: 0.0, u: 5.0, v: 0.0, r: 0.0 }, // 9.7 kn
        controlState: { rudderAngle: 0.0, throttle: 0.0 }
      },
      lifecycleStatus: { sim_time: 100 }
    } as any);

    render(<SimulationMonitor />);
    const shipTab = screen.getByTestId('left-tab-ship');
    fireEvent.click(shipTab);

    // Expect source badge to render YAML source label
    expect(screen.getByText('YAML 场景配置')).toBeInTheDocument();
    // Expect target speed calculated: 12.0 kn
    expect(screen.getByText('12.0 kn')).toBeInTheDocument();
    // Expect waypoint count to render WP02
    expect(screen.getByText('WP02')).toBeInTheDocument();
  });

  it('displays L2 实时系统 when L2 voyagePlan telemetry is active and updates badge dynamically', () => {
    useTelemetryStore.setState({
      wsConnected: true,
      ownShip: {
        pose: { lat: 63.4, lon: 10.4, heading: 0.0 },
        kinematics: { sog: 0.0, cog: 0.0, rot: 0.0, u: 0.0, v: 0.0, r: 0.0 },
        controlState: { rudderAngle: 0.0, throttle: 0.0 }
      },
      lifecycleStatus: { sim_time: 100 },
      voyagePlan: {
        waypoints: [
          { lat: 63.4, lon: 10.4 },
          { lat: 63.6, lon: 10.4 }
        ],
        cruiseSpeed: 8.5,
        source: 'l2_realtime'
      }
    } as any);

    render(<SimulationMonitor />);
    const shipTab = screen.getByTestId('left-tab-ship');
    fireEvent.click(shipTab);

    // Expect source badge to render L2 real-time label
    expect(screen.getByText('L2 实时系统')).toBeInTheDocument();
    // Expect target speed to render cruiseSpeed from store: 8.5 kn
    expect(screen.getByText('8.5 kn')).toBeInTheDocument();
    // Expect waypoint count to render WP02
    expect(screen.getByText('WP02')).toBeInTheDocument();
  });

  it('renders real voyage plan progress from current route leg and speed profile', () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-06-17T05:00:00.000Z'));

    useTelemetryStore.setState({
      wsConnected: true,
      ownShip: {
        pose: { lat: 63.45, lon: 10.4, heading: 0.0 },
        kinematics: { sog: 0.0, cog: 0.0, rot: 0.0, u: 0.0, v: 0.0, r: 0.0 },
        controlState: { rudderAngle: 0.0, throttle: 0.0 }
      },
      lifecycleStatus: { sim_time: 300 },
      voyagePlan: {
        waypoints: [
          { lat: 63.4, lon: 10.4 },
          { lat: 63.5, lon: 10.4 },
          { lat: 63.5, lon: 10.5 },
        ],
        cruiseSpeed: 10.0,
        speedProfileKn: [8.0, 12.0],
        source: 'l2_realtime'
      }
    } as any);

    render(<SimulationMonitor />);
    fireEvent.click(screen.getByTestId('left-tab-ship'));

    expect(screen.getByText('L2 实时系统')).toBeInTheDocument();
    expect(screen.getByText('WP02')).toBeInTheDocument();
    expect(screen.getByText('5.7 nm')).toBeInTheDocument();
    expect(screen.getByText('8.0 kn')).toBeInTheDocument();
    expect(screen.getByText('剩余航时 ETA')).toBeInTheDocument();
    expect(screen.getByText('35.9 min')).toBeInTheDocument();
  });

  it('displays CPA and TCPA from M2 world-state target metrics', () => {
    useTelemetryStore.setState({
      wsConnected: true,
      ownShip: {
        pose: { lat: 63.4, lon: 10.4, heading: 0.0 },
        kinematics: { sog: 5.0, cog: 0.0, rot: 0.0, u: 5.0, v: 0.0, r: 0.0 },
        controlState: { rudderAngle: 0.0, throttle: 0.0 },
      },
      targets: [{
        mmsi: 100000001,
        pose: { lat: 63.405, lon: 10.4, heading: Math.PI },
        kinematics: { sog: 6.17, cog: Math.PI, rot: 0.0 },
        cpaM: 926.0,
        tcpaS: 180.0,
      }],
    } as any);

    render(<SimulationMonitor />);
    fireEvent.click(screen.getByTestId('left-tab-threat'));

    expect(screen.getByText('最近会遇 CPA')).toBeInTheDocument();
    expect(screen.getByTestId('threat-cpa')).toHaveTextContent('0.50 nm');
    expect(screen.getByText('会遇时间 TCPA')).toBeInTheDocument();
    expect(screen.getByText('3.0 min')).toBeInTheDocument();
  });

  it('renders avoidance decision cards from real module telemetry instead of mock defaults', () => {
    useFsmStore.setState({
      currentState: 'COLREG_AVOIDANCE',
      activeRule: 'Rule 14 head-on',
      confidence: 0.92,
      transitionHistory: [],
      torRequest: null,
    } as any);
    useTelemetryStore.setState({
      wsConnected: true,
      ownShip: {
        pose: { lat: 63.4, lon: 10.4, heading: 0.0 },
        kinematics: { sog: 5.0, cog: 0.0, rot: 0.0, u: 5.0, v: 0.0, r: 0.0 },
        controlState: { rudderAngle: 0.0, throttle: 0.0 },
      },
      oddState: {
        envelopeState: 1,
        zone: 1,
        health: 1,
        conformanceScore: 0.88,
        confidence: 0.93,
      },
      colregsConstraint: {
        ruleId: 14,
        role: 1,
        preferredDirection: 'STARBOARD',
        minAlterationDeg: 22,
        phase: 'T_act',
        confidence: 0.91,
      },
      behaviorPlan: {
        behavior: 1,
        headingMinDeg: 15,
        headingMaxDeg: 35,
        speedMinKn: 6,
        speedMaxKn: 9,
        confidence: 0.81,
      },
      avoidancePlan: {
        waypoints: [{ lat: 63.41, lon: 10.42, targetSpeedKn: 8.2, turnRadiusM: 300 }],
        status: 'NORMAL',
        confidence: 0.87,
      },
      safetyAlert: {
        alertType: 1,
        severity: 2,
        description: 'SOTIF assumption breach',
        recommendedMrm: 'MRM-02',
        confidence: 0.75,
      },
      sotifMetrics: { checker_veto_rate_pct: 3.5 },
    } as any);

    render(<SimulationMonitor />);
    fireEvent.click(screen.getByTestId('left-tab-avoid'));

    expect(screen.getByText('EDGE')).toBeInTheDocument();
    expect(screen.getByText('COLREG AVOIDANCE')).toBeInTheDocument();
    expect(screen.getByText('Rule 14')).toBeInTheDocument();
    expect(screen.getByText('GIVE-WAY 让路')).toBeInTheDocument();
    expect(screen.getByText('STARBOARD 22°')).toBeInTheDocument();
    expect(screen.getByText('NORMAL / 8.2 kn')).toBeInTheDocument();
    expect(screen.getByText('3.5%')).toBeInTheDocument();
    expect(screen.getByText('CRITICAL / MRM-02')).toBeInTheDocument();

    expect(screen.queryByText('NONE')).not.toBeInTheDocument();
    expect(screen.queryByText('右舵转向 15°')).not.toBeInTheDocument();
    expect(screen.queryByText('正常运行 (LV 0)')).not.toBeInTheDocument();
  });

  it('renders bottom M1-M8 module popovers from the same real telemetry as the side panels', () => {
    useFsmStore.setState({
      currentState: 'COLREG_AVOIDANCE',
      activeRule: 'Rule 14 head-on',
      confidence: 0.92,
      transitionHistory: [],
      torRequest: null,
    } as any);
    useTelemetryStore.setState({
      wsConnected: true,
      ownShip: {
        pose: { lat: 63.45, lon: 10.4, heading: 0.0 },
        kinematics: { sog: 5.0, cog: 0.0, rot: 0.0, u: 5.0, v: 0.0, r: 0.0 },
        controlState: { rudderAngle: 0.0, throttle: 0.0 },
      },
      lifecycleStatus: { sim_time: 300 },
      targets: [{
        mmsi: 100000001,
        pose: { lat: 63.455, lon: 10.4, heading: Math.PI },
        kinematics: { sog: 6.17, cog: Math.PI, rot: 0.0 },
        cpaM: 926.0,
        tcpaS: 180.0,
      }],
      voyagePlan: {
        waypoints: [
          { lat: 63.4, lon: 10.4 },
          { lat: 63.5, lon: 10.4 },
          { lat: 63.5, lon: 10.5 },
        ],
        cruiseSpeed: 10.0,
        speedProfileKn: [8.0, 12.0],
        source: 'l2_realtime',
      },
      oddState: {
        envelopeState: 1,
        zone: 1,
        health: 2,
        conformanceScore: 0.88,
        confidence: 0.93,
      },
      colregsConstraint: {
        ruleId: 14,
        role: 1,
        preferredDirection: 'STARBOARD',
        minAlterationDeg: 22,
        phase: 'T_act',
        confidence: 0.91,
      },
      behaviorPlan: {
        behavior: 1,
        headingMinDeg: 15,
        headingMaxDeg: 35,
        speedMinKn: 6,
        speedMaxKn: 9,
        confidence: 0.81,
      },
      avoidancePlan: {
        waypoints: [{ lat: 63.41, lon: 10.42, targetSpeedKn: 8.2, turnRadiusM: 300 }],
        horizonS: 120,
        status: 'NORMAL',
        confidence: 0.87,
      },
      safetyAlert: {
        alertType: 1,
        severity: 2,
        description: 'SOTIF assumption breach',
        recommendedMrm: 'MRM-02',
        confidence: 0.75,
      },
      sotifMetrics: {
        checker_veto_rate_pct: 3.5,
        comm_link_rtt_ms: 18,
        perception_coverage_pct: 97,
      },
      sat2: {
        active_behavior: 'COLREG_AVOID',
        active_behavior_weight: 0.8,
        ivp_contributions: [],
        colregs_chain_target_id: '990000001',
        reasoning_latency_ms: 2.4,
        colregs_chain: [
          { layer: 1, label: 'ODD', conclusion: 'Rule15', confidence: 1.0, inputs: {} },
          { layer: 2, label: 'encounter', conclusion: 'CROSSING', confidence: 0.5, inputs: {} },
          { layer: 3, label: 'duty', conclusion: 'give_way', confidence: 1.0, inputs: {} },
        ],
      },
    } as any);

    render(<SimulationMonitor />);

    fireEvent.click(screen.getByText('M1'));
    expect(screen.getByText('EDGE')).toBeInTheDocument();
    expect(screen.getByText('88%')).toBeInTheDocument();

    fireEvent.click(screen.getByText('M2'));
    expect(screen.getByText('1')).toBeInTheDocument();
    expect(screen.getByText('0.50 nm')).toBeInTheDocument();

    fireEvent.click(screen.getByText('M3'));
    expect(screen.getByText('WP02')).toBeInTheDocument();
    expect(screen.getByText('35.9 min')).toBeInTheDocument();

    fireEvent.click(screen.getByText('M4'));
    expect(screen.getByText('COLREG_AVOID')).toBeInTheDocument();
    expect(screen.getByText('15°-35°')).toBeInTheDocument();

    fireEvent.click(screen.getByText('M5'));
    expect(screen.getByText('NORMAL / 8.2 kn')).toBeInTheDocument();
    expect(screen.getByText('1 条')).toBeInTheDocument();

    fireEvent.click(screen.getByText('M6'));
    expect(screen.getByText('Rule 14')).toBeInTheDocument();
    expect(screen.getByText('GIVE-WAY 让路')).toBeInTheDocument();
    expect(screen.queryByText('M6 COLREGs REASONING')).not.toBeInTheDocument();
    expect(screen.queryByText('Rule15')).not.toBeInTheDocument();
    expect(screen.queryByText('CROSSING')).not.toBeInTheDocument();
    expect(screen.queryByText('give_way')).not.toBeInTheDocument();

    fireEvent.click(screen.getByText('M7'));
    expect(screen.getByText('CRITICAL / MRM-02')).toBeInTheDocument();
    expect(screen.getByText('3.5%')).toBeInTheDocument();

    fireEvent.click(screen.getByText('M8'));
    expect(screen.getByText('LIVE')).toBeInTheDocument();
    expect(screen.getByText('18 ms')).toBeInTheDocument();

    expect(screen.queryByText('OPEN_WATER (开阔)')).not.toBeInTheDocument();
    expect(screen.queryByText('SEG_XIAMEN_SHANGHAI_A')).not.toBeInTheDocument();
    expect(screen.queryByText('No active COLREGs encounter')).not.toBeInTheDocument();
  });

  it('redirects to preflight and clears stale telemetry when monitor URL conflicts with active lifecycle scenario', () => {
    window.location.hash = '#/monitor/colreg-rule14-ho';
    vi.mocked(useGetLifecycleStatusQuery).mockReturnValue({
      data: { scenario_id: 'colreg-rule14-ho-port', current_state: 3 },
    } as any);
    useTelemetryStore.setState({
      wsConnected: true,
      lifecycleStatus: { scenario_id: 'colreg-rule14-ho-port', current_state: 3, sim_time: 12 },
      ownShip: {
        pose: { lat: 63.4, lon: 10.4, heading: 0.0 },
        kinematics: { sog: 5.0, cog: 0.0, rot: 0.0, u: 5.0, v: 0.0, r: 0.0 },
        controlState: { rudderAngle: 0.0, throttle: 0.0 },
      },
      ownShipTrail: [[10.4, 63.4]],
      targets: [{ mmsi: 123, pose: { lat: 63.41, lon: 10.41, heading: 0.0 } }],
    } as any);

    render(<SimulationMonitor routeScenarioId="colreg-rule14-ho" />);

    expect(window.location.hash).toBe('#/check/colreg-rule14-ho');
    expect(useTelemetryStore.getState().ownShip).toBeNull();
    expect(useTelemetryStore.getState().ownShipTrail).toEqual([]);
    expect(useTelemetryStore.getState().targets).toEqual([]);
  });

  it('does not bounce back to preflight when fresh local lifecycle telemetry already matches the monitor route', () => {
    window.location.hash = '#/monitor/colreg-rule15-cs-2';
    vi.mocked(useGetLifecycleStatusQuery).mockReturnValue({
      data: { scenario_id: 'colreg-rule14-ho', current_state: 3 },
    } as any);
    useTelemetryStore.setState({
      wsConnected: true,
      lifecycleStatus: { scenario_id: 'colreg-rule15-cs-2', current_state: 3, sim_time: 1 },
      ownShip: {
        pose: { lat: 63.4, lon: 10.4, heading: 0.0 },
        kinematics: { sog: 5.0, cog: 0.0, rot: 0.0, u: 5.0, v: 0.0, r: 0.0 },
        controlState: { rudderAngle: 0.0, throttle: 0.0 },
      },
      ownShipTrail: [[10.4, 63.4]],
      targets: [{ mmsi: 123, pose: { lat: 63.41, lon: 10.41, heading: 0.0 } }],
    } as any);

    render(<SimulationMonitor routeScenarioId="colreg-rule15-cs-2" />);

    expect(window.location.hash).toBe('#/monitor/colreg-rule15-cs-2');
    expect(useTelemetryStore.getState().ownShip).not.toBeNull();
    expect(useTelemetryStore.getState().ownShipTrail).toEqual([[10.4, 63.4]]);
    expect(useTelemetryStore.getState().targets).toHaveLength(1);
  });
});
