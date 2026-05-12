import { create } from 'zustand';
import type { OwnShipState, TargetVesselState, EnvironmentState, ModulePulse } from '../types';

interface TelemetryState {
  ownShip: OwnShipState | null;
  targets: TargetVesselState[];
  environment: EnvironmentState | null;
  modulePulses: ModulePulse[];

  updateOwnShip: (state: OwnShipState) => void;
  updateTargets: (targets: TargetVesselState[]) => void;
  updateEnvironment: (env: EnvironmentState) => void;
  updateModulePulses: (pulses: ModulePulse[]) => void;
  reset: () => void;
}

const initialState = {
  ownShip: null,
  targets: [],
  environment: null,
  modulePulses: [],
};

export const useTelemetryStore = create<TelemetryState>((set) => ({
  ...initialState,
  updateOwnShip: (ownShip) => set({ ownShip }),
  updateTargets: (targets) => set({ targets }),
  updateEnvironment: (environment) => set({ environment }),
  updateModulePulses: (modulePulses) => set({ modulePulses }),
  reset: () => set(initialState),
}));
