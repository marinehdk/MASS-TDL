import { describe, it, expect, beforeEach } from 'vitest';
import { useFsmStore } from '../fsmStore';

describe('fsmStore', () => {
  beforeEach(() => {
    useFsmStore.setState({
      currentState: 'TRANSIT',
      transitionHistory: [],
      torRequest: null,
    });
  });

  it('initial state is TRANSIT', () => {
    expect(useFsmStore.getState().currentState).toBe('TRANSIT');
  });

  it('setState records transition', () => {
    const store = useFsmStore.getState();
    store.setState('COLREG_AVOIDANCE', 'CPA_BELOW_THRESHOLD', 100);

    const state = useFsmStore.getState();
    expect(state.currentState).toBe('COLREG_AVOIDANCE');
    expect(state.transitionHistory).toHaveLength(1);
    expect(state.transitionHistory[0]).toMatchObject({
      from: 'TRANSIT',
      to: 'COLREG_AVOIDANCE',
      reason: 'CPA_BELOW_THRESHOLD',
      timestamp: 100,
    });
  });

  it('transitionHistory capped at 100', () => {
    const store = useFsmStore.getState();
    const states = ['COLREG_AVOIDANCE', 'TOR', 'OVERRIDE', 'MRC', 'HANDBACK', 'TRANSIT'] as const;
    for (let i = 0; i < 120; i++) {
      store.setState(states[i % states.length], `reason_${i}`, i);
    }
    expect(useFsmStore.getState().transitionHistory).toHaveLength(100);
  });

  it('setTorRequest stores request', () => {
    const req = {
      reason: 'M7 Veto',
      triggeredAtSimTime: 200,
      tmrDeadlineSimTime: 260,
      sat1LockUntilSimTime: 205,
      currentSituation: 'Target too close',
      proposedAction: 'Starboard turn',
    };
    useFsmStore.getState().setTorRequest(req);
    expect(useFsmStore.getState().torRequest).toEqual(req);
  });

  it('setTorRequest(null) clears request', () => {
    useFsmStore.getState().setTorRequest({
      reason: 'test', triggeredAtSimTime: 0, tmrDeadlineSimTime: 60,
      sat1LockUntilSimTime: 5, currentSituation: '', proposedAction: '',
    });
    useFsmStore.getState().setTorRequest(null);
    expect(useFsmStore.getState().torRequest).toBeNull();
  });

  it('clearHistory empties transition history', () => {
    const store = useFsmStore.getState();
    store.setState('COLREG_AVOIDANCE', 'test', 0);
    store.clearHistory();
    expect(useFsmStore.getState().transitionHistory).toEqual([]);
  });
});
