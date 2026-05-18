import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';

const mockMap = {
  addControl: vi.fn(), remove: vi.fn(),
  on: vi.fn(), off: vi.fn(),
  addSource: vi.fn(), addLayer: vi.fn(),
  getSource: vi.fn(() => ({ setData: vi.fn() })),
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
    const handler = (e: KeyboardEvent) => { if (handlers[e.key]) handlers[e.key](); };
    document.addEventListener('keydown', handler);
    return () => document.removeEventListener('keydown', handler);
  }),
}));
vi.mock('../../api/silApi', () => ({
  useDeactivateLifecycleMutation: () => [vi.fn().mockResolvedValue({})],
}));

import { SimulationMonitor } from '../SimulationMonitor';
import { useUIStore } from '../../store/uiStore';
import { useFsmStore } from '../../store/fsmStore';
import { useTelemetryStore } from '../../store';

beforeEach(() => {
  useUIStore.getState().reset();
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

  it('engineer view shows left drawer toggle', () => {
    useUIStore.getState().setViewMode('engineer');
    render(<SimulationMonitor />);
    expect(screen.getByTestId('left-drawer-toggle')).toBeInTheDocument();
  });

  it('left drawer toggle opens drawer', () => {
    useUIStore.getState().setViewMode('engineer');
    render(<SimulationMonitor />);
    fireEvent.click(screen.getByTestId('left-drawer-toggle'));
    expect(useUIStore.getState().leftDrawerOpen).toBe(true);
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
