import { create } from 'zustand';

export type ViewMode = 'captain' | 'engineer' | 'roc';

interface UIState {
  viewMode: ViewMode;
  leftDrawerOpen: boolean;
  rightDrawerOpen: boolean;
  asdrLogExpanded: boolean;
  pulseBarExpanded: boolean;

  setViewMode: (mode: ViewMode) => void;
  toggleLeftDrawer: () => void;
  toggleRightDrawer: () => void;
  toggleAsdrLog: () => void;
  togglePulseBar: () => void;
  reset: () => void;
}

export const useUIStore = create<UIState>((set) => ({
  viewMode: 'captain',
  leftDrawerOpen: false,
  rightDrawerOpen: false,
  asdrLogExpanded: false,
  pulseBarExpanded: false,
  setViewMode: (viewMode) => set({ viewMode }),
  toggleLeftDrawer: () => set((s) => ({ leftDrawerOpen: !s.leftDrawerOpen })),
  toggleRightDrawer: () => set((s) => ({ rightDrawerOpen: !s.rightDrawerOpen })),
  toggleAsdrLog: () => set((s) => ({ asdrLogExpanded: !s.asdrLogExpanded })),
  togglePulseBar: () => set((s) => ({ pulseBarExpanded: !s.pulseBarExpanded })),
  reset: () => set({
    viewMode: 'captain',
    leftDrawerOpen: false,
    rightDrawerOpen: false,
    asdrLogExpanded: false,
    pulseBarExpanded: false,
  }),
}));
