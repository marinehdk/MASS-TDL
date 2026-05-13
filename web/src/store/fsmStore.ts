import { create } from 'zustand';

export type FsmState = 'TRANSIT' | 'COLREG_AVOIDANCE' | 'TOR' | 'OVERRIDE' | 'MRC' | 'HANDBACK';

export interface FsmTransition {
  from: FsmState;
  to: FsmState;
  reason: string;
  timestamp: number;  // sim_time seconds
}

export interface TorRequest {
  reason: string;
  triggeredAtSimTime: number;
  tmrDeadlineSimTime: number;       // +60s by default
  sat1LockUntilSimTime: number;     // +5s SAT-1 lock
  currentSituation: string;
  proposedAction: string;
}

interface FsmStore {
  currentState: FsmState;
  transitionHistory: FsmTransition[];
  torRequest: TorRequest | null;
  setState: (next: FsmState, reason: string, simTime: number) => void;
  setTorRequest: (req: TorRequest | null) => void;
  clearHistory: () => void;
}

export const useFsmStore = create<FsmStore>((set) => ({
  currentState: 'TRANSIT',
  transitionHistory: [],
  torRequest: null,
  setState: (next, reason, simTime) => set((s) => ({
    currentState: next,
    transitionHistory: [
      ...s.transitionHistory,
      { from: s.currentState, to: next, reason, timestamp: simTime },
    ].slice(-100),
  })),
  setTorRequest: (req) => set({ torRequest: req }),
  clearHistory: () => set({ transitionHistory: [] }),
}));
