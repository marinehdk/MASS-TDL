import { create } from 'zustand';

export interface MapViewport {
  center: [number, number];  // [lng, lat]
  zoom: number;
  bearing: number;           // degrees, 0 = North-up
  pitch: number;
}

const DEFAULT_VIEWPORT: MapViewport = {
  center: [10.38, 63.44],
  zoom: 12,
  bearing: 0,
  pitch: 0,
};

interface MapStore {
  viewport: MapViewport;
  setViewport: (v: Partial<MapViewport>) => void;
  resetViewport: () => void;
}

export const useMapStore = create<MapStore>((set) => ({
  viewport: { ...DEFAULT_VIEWPORT },
  setViewport: (partial) => set((s) => ({ viewport: { ...s.viewport, ...partial } })),
  resetViewport: () => set({ viewport: { ...DEFAULT_VIEWPORT } }),
}));
