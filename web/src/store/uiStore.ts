import { create } from 'zustand';

export type ViewMode = 'captain' | 'engineer' | 'roc' | 'god';

interface UIState {
  viewMode: ViewMode;
  leftDrawerOpen: boolean;
  rightDrawerOpen: boolean;
  asdrLogExpanded: boolean;
  pulseBarExpanded: boolean;
  selectedVesselId: string | null; // 'ownship' or target's mmsi string

  setViewMode: (mode: ViewMode) => void;
  toggleLeftDrawer: () => void;
  toggleRightDrawer: () => void;
  toggleAsdrLog: () => void;
  togglePulseBar: () => void;
  setSelectedVesselId: (id: string | null) => void;
  reset: () => void;
}

export const useUIStore = create<UIState>((set) => ({
  viewMode: 'god',
  leftDrawerOpen: false,
  rightDrawerOpen: false,
  asdrLogExpanded: false,
  pulseBarExpanded: false,
  selectedVesselId: 'ownship',
  setViewMode: (viewMode) => set({ viewMode }),
  toggleLeftDrawer: () => set((s) => ({ leftDrawerOpen: !s.leftDrawerOpen })),
  toggleRightDrawer: () => set((s) => ({ rightDrawerOpen: !s.rightDrawerOpen })),
  toggleAsdrLog: () => set((s) => ({ asdrLogExpanded: !s.asdrLogExpanded })),
  togglePulseBar: () => set((s) => ({ pulseBarExpanded: !s.pulseBarExpanded })),
  setSelectedVesselId: (selectedVesselId) => set({ selectedVesselId }),
  reset: () => set({
    viewMode: 'god',
    leftDrawerOpen: false,
    rightDrawerOpen: false,
    asdrLogExpanded: false,
    pulseBarExpanded: false,
    selectedVesselId: 'ownship',
  }),
}));
