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
  jumpTo: vi.fn(), easeTo: vi.fn(), setPadding: vi.fn(),
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
  useGetScenarioQuery: vi.fn(() => ({ data: null, isLoading: false })),
}));

import { useGetScenarioQuery } from '../../api/silApi';
import { SimulationMonitor } from '../SimulationMonitor';
import { useUIStore } from '../../store/uiStore';
import { useFsmStore } from '../../store/fsmStore';
import { useTelemetryStore } from '../../store';

beforeEach(() => {
  useUIStore.getState().reset();
  useUIStore.getState().setViewMode('captain');
  useFsmStore.setState({ currentState: 'TRANSIT', transitionHistory: [], torRequest: null });
  useTelemetryStore.setState({ wsConnected: true, ownShip: null } as any);
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
});
