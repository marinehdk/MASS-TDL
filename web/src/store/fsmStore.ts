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
  tmrDeadlineSimTime: number;        // triggeredAtSimTime + 60s
  currentSituation: string;
  proposedAction: string;
  recommendedMrm?: 'MRM-01' | 'MRM-02' | 'MRM-03' | 'MRM-04';
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
