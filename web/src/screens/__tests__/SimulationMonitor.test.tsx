import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';

const mockMap = {
  addControl: vi.fn(), remove: vi.fn(),
  on: vi.fn(), off: vi.fn(),
  addSource: vi.fn(), addLayer: vi.fn(),
  getSource: vi.fn(() => ({ setData: vi.fn() })),
  getLayer: vi.fn(() => null),
  getCenter: vi.fn(() => ({ lng: 10.4, lat: 63.4 })),
  getContainer: vi.fn(() => ({ clientHeight: 800 })),
  jumpTo: vi.fn(), easeTo: vi.fn(), setPadding: vi.fn(),
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  setPaintProperty: vi.fn(), setLayoutProperty: vi.fn(),
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
}));

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
    expect(screen.queryByText('本船实步数据')).not.toBeInTheDocument();
    
    // Click tab -> expands panel
    fireEvent.click(shipTab);
    expect(screen.getByText('本船实步数据')).toBeInTheDocument();
    
    // Click tab again -> collapses panel
    fireEvent.click(shipTab);
    expect(screen.queryByText('本船实步数据')).not.toBeInTheDocument();
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
});
