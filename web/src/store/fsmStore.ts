import { create } from 'zustand';

export type FsmState = 'TRANSIT' | 'COLREG_AVOIDANCE' | 'TOR' | 'OVERRIDE' | 'MRC' | 'HANDBACK';

export interface FsmTransition {
  from: FsmState;
  to: FsmState;
  reason: string;
  timestamp: number;
}

export interface TorRequest {
  reason: string;
  triggeredAtSimTime: number;
  tmrDeadlineSimTime: number;
  currentSituation: string;
  proposedAction: string;
  recommendedMrm?: 'MRM-01' | 'MRM-02' | 'MRM-03' | 'MRM-04';
}

interface FsmStore {
  currentState: FsmState;
  activeRule: string;
  confidence: number;
  transitionHistory: FsmTransition[];
  torRequest: TorRequest | null;

  _updateState: (
    state: FsmState,
    rule: string,
    confidence: number,
    simTime: number
  ) => void;
  setTorRequest: (req: TorRequest | null) => void;
  clearHistory: () => void;

  setState: (next: FsmState, reason: string, simTime: number) => void;

  _devToggleState: (simTime: number) => void;
}

export const useFsmStore = create<FsmStore>((set, get) => ({
  currentState: 'TRANSIT',
  activeRule: 'Nominal autopilot',
  confidence: 0.95,
  transitionHistory: [],
  torRequest: null,

  _updateState: (nextState, rule, conf, simTime) => set((s) => {
    if (s.currentState === nextState && s.activeRule === rule) {
      return { confidence: conf };
    }
    const transition: FsmTransition = {
      from: s.currentState,
      to: nextState,
      reason: rule,
      timestamp: simTime,
    };
    return {
      currentState: nextState,
      activeRule: rule,
      confidence: conf,
      transitionHistory: [
        ...s.transitionHistory,
        transition,
      ].slice(-100),
    };
  }),

  setTorRequest: (req) => set({ torRequest: req }),
  clearHistory: () => set({ transitionHistory: [] }),

  setState: (next, reason, simTime) => set((s) => ({
    currentState: next,
    activeRule: reason,
    transitionHistory: [
      ...s.transitionHistory,
      { from: s.currentState, to: next, reason, timestamp: simTime },
    ].slice(-100),
  })),

  _devToggleState: (simTime) => {
    const { currentState } = get();
    if (currentState === 'TRANSIT') {
      get()._updateState('COLREG_AVOIDANCE', '[DEV-TOGGLE] Rule 14', 0.2, simTime);
    } else if (currentState === 'COLREG_AVOIDANCE') {
      get()._updateState('TRANSIT', '[DEV-TOGGLE] Handback', 0.2, simTime);
    }
  },
}));

if (import.meta.env.DEV) {
  (window as any).__FSM_STORE__ = useFsmStore;
}

export const FSM_STATE_MAP: Record<number, FsmState> = {
  0: 'TRANSIT',
  1: 'COLREG_AVOIDANCE',
  2: 'TOR',
  3: 'OVERRIDE',
  4: 'MRC',
  5: 'HANDBACK',
};
