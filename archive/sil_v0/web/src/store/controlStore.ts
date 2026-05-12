import { create } from 'zustand';

interface ControlState {
  simRate: number;
  isPaused: boolean;
  activeFaults: string[];

  setSimRate: (rate: number) => void;
  setPaused: (paused: boolean) => void;
  setActiveFaults: (faults: string[]) => void;
  reset: () => void;
}

export const useControlStore = create<ControlState>((set) => ({
  simRate: 1,
  isPaused: false,
  activeFaults: [],
  setSimRate: (simRate) => set({ simRate }),
  setPaused: (isPaused) => set({ isPaused }),
  setActiveFaults: (activeFaults) => set({ activeFaults }),
  reset: () => set({ simRate: 1, isPaused: false, activeFaults: [] }),
}));
