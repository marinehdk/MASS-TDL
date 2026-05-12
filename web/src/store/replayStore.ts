import { create } from 'zustand';

interface ReplayState {
  scrubTime: number;
  mcapDuration: number;
  isScrubbing: boolean;

  setScrubTime: (t: number) => void;
  setDuration: (d: number) => void;
  setScrubbing: (s: boolean) => void;
  reset: () => void;
}

export const useReplayStore = create<ReplayState>((set) => ({
  scrubTime: 0,
  mcapDuration: 0,
  isScrubbing: false,
  setScrubTime: (scrubTime) => set({ scrubTime }),
  setDuration: (mcapDuration) => set({ mcapDuration }),
  setScrubbing: (isScrubbing) => set({ isScrubbing }),
  reset: () => set({ scrubTime: 0, mcapDuration: 0, isScrubbing: false }),
}));
