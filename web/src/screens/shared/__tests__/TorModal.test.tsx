import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, screen, fireEvent, act } from '@testing-library/react';
import { TorModal } from '../TorModal';
import { useFsmStore } from '../../../store/fsmStore';
import { useTelemetryStore } from '../../../store';

beforeEach(() => {
  useFsmStore.setState({
    currentState: 'TOR',
    transitionHistory: [],
    torRequest: {
      reason: 'M7 VETO: AIS consistency >2σ',
      triggeredAtSimTime: 100,
      tmrDeadlineSimTime: 160,
      currentSituation: 'Target CPA 0.3nm',
      proposedAction: 'Starboard 30°',
      recommendedMrm: 'MRM-01',
    },
  });
  useTelemetryStore.setState({ lifecycleStatus: { sim_time: 110 } } as any);
});

afterEach(() => {
  useFsmStore.getState().clearHistory();
  useFsmStore.getState().setTorRequest(null);
});

describe('TorModal', () => {
  it('renders when FSM is TOR', () => {
    render(<TorModal />);
    expect(screen.getByTestId('tor-modal')).toBeInTheDocument();
  });

  it('does not render when FSM is TRANSIT', () => {
    useFsmStore.setState({ currentState: 'TRANSIT' });
    render(<TorModal />);
    expect(screen.queryByTestId('tor-modal')).toBeNull();
  });

  it('TAKE OVER button is present', () => {
    render(<TorModal />);
    expect(screen.getByTestId('tor-take-control')).toBeInTheDocument();
  });

  it('pointer-down shows hold progress bar', () => {
    vi.useFakeTimers();
    render(<TorModal />);
    const btn = screen.getByTestId('tor-take-control');
    fireEvent.pointerDown(btn);
    act(() => { vi.advanceTimersByTime(60); });
    expect(screen.getByTestId('tor-hold-progress')).toBeInTheDocument();
    vi.useRealTimers();
  });

  it('pointer-up before 2s does NOT transition to OVERRIDE', () => {
    vi.useFakeTimers();
    render(<TorModal />);
    const btn = screen.getByTestId('tor-take-control');
    fireEvent.pointerDown(btn);
    act(() => { vi.advanceTimersByTime(800); });
    fireEvent.pointerUp(btn);
    expect(useFsmStore.getState().currentState).toBe('TOR');
    vi.useRealTimers();
  });

  it('holding ≥2s transitions to OVERRIDE', () => {
    vi.useFakeTimers();
    render(<TorModal />);
    const btn = screen.getByTestId('tor-take-control');
    fireEvent.pointerDown(btn);
    act(() => { vi.advanceTimersByTime(2100); });
    fireEvent.pointerUp(btn);
    expect(useFsmStore.getState().currentState).toBe('OVERRIDE');
    vi.useRealTimers();
  });

  it('DECLINE button triggers MRC', () => {
    render(<TorModal />);
    fireEvent.click(screen.getByTestId('tor-decline'));
    expect(useFsmStore.getState().currentState).toBe('MRC');
  });

  it('shows countdown in seconds', () => {
    render(<TorModal />);
    expect(screen.getByTestId('tor-countdown').textContent).toMatch(/50/);
  });
});
