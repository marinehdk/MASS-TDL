import { create } from 'zustand';
import type { OwnShipState, TargetVesselState, EnvironmentState, ModulePulse } from '../types';

// ------------------------------------------------------------------
// Lightweight local types for topics not yet in generated Protobuf TS
// ------------------------------------------------------------------
export interface ASDREvent {
  stamp?: { seconds?: number };
  event_type: string;
  rule_ref?: string;
  decision_id?: string;
  verdict?: 0 | 1 | 2 | 3; // UNKNOWN / PASS / RISK / FAIL
  payload_json?: string;
}

export interface LifecycleStatus {
  current_state?: number; // 0=UNKNOWN 1=UNCONFIGURED 2=INACTIVE 3=ACTIVE 4=DEACTIVATING 5=FINALIZED
  scenario_id?: string;
  scenario_hash?: string;
  sim_time?: number;
  wall_time?: number;
  sim_rate?: number;
}

const MAX_ASDR = 200;
const MAX_TRAIL = 600; // 600 points × 50Hz = 12 s of trail

interface TelemetryState {
  // Core telemetry
  ownShip: OwnShipState | null;
  targets: TargetVesselState[];
  environment: EnvironmentState | null;
  modulePulses: ModulePulse[];
  // New in DEMO-1 revision
  asdrEvents: ASDREvent[];
  lifecycleStatus: LifecycleStatus | null;
  wsConnected: boolean;
  /** [lon, lat] pairs for own-ship trajectory trail */
  ownShipTrail: [number, number][];

  updateOwnShip: (state: OwnShipState) => void;
  updateTargets: (targets: TargetVesselState[]) => void;
  updateEnvironment: (env: EnvironmentState) => void;
  updateModulePulses: (pulses: ModulePulse[]) => void;
  appendAsdrEvent: (evt: ASDREvent) => void;
  updateLifecycleStatus: (status: LifecycleStatus) => void;
  setWsConnected: (v: boolean) => void;
  reset: () => void;
}

const initialState = {
  ownShip: null,
  targets: [],
  environment: null,
  modulePulses: [],
  asdrEvents: [],
  lifecycleStatus: null,
  wsConnected: false,
  ownShipTrail: [] as [number, number][],
};

export const useTelemetryStore = create<TelemetryState>((set) => ({
  ...initialState,
  updateOwnShip: (ownShip) => set((s) => {
    // Append to trail if coordinates are valid
    const lon = ownShip.pose?.lon;
    const lat = ownShip.pose?.lat;
    if (typeof lon === 'number' && typeof lat === 'number') {
      const trail = [...s.ownShipTrail, [lon, lat] as [number, number]];
      return { ownShip, ownShipTrail: trail.length > MAX_TRAIL ? trail.slice(-MAX_TRAIL) : trail };
    }
    return { ownShip };
  }),
  updateTargets: (targets) => set({ targets }),
  updateEnvironment: (environment) => set({ environment }),
  updateModulePulses: (modulePulses) => set({ modulePulses }),
  appendAsdrEvent: (evt) => set((s) => ({
    asdrEvents: s.asdrEvents.length >= MAX_ASDR
      ? [...s.asdrEvents.slice(1), evt]
      : [...s.asdrEvents, evt],
  })),
  updateLifecycleStatus: (lifecycleStatus) => set({ lifecycleStatus }),
  setWsConnected: (wsConnected) => set({ wsConnected }),
  reset: () => set(initialState),
}));
