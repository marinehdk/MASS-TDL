import { create } from 'zustand';

type ViewMode = 'captain' | 'god' | 'roc';

interface UIState {
  viewMode: ViewMode;
  asdrLogExpanded: boolean;
  pulseBarExpanded: boolean;

  setViewMode: (mode: ViewMode) => void;
  toggleAsdrLog: () => void;
  togglePulseBar: () => void;
  reset: () => void;
}

export const useUIStore = create<UIState>((set) => ({
  viewMode: 'captain',
  asdrLogExpanded: false,
  pulseBarExpanded: false,
  setViewMode: (viewMode) => set({ viewMode }),
  toggleAsdrLog: () => set((s) => ({ asdrLogExpanded: !s.asdrLogExpanded })),
  togglePulseBar: () => set((s) => ({ pulseBarExpanded: !s.pulseBarExpanded })),
  reset: () => set({ viewMode: 'captain', asdrLogExpanded: false, pulseBarExpanded: false }),
}));
