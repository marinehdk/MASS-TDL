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

export interface SensorState {
  id: string;
  state: 'ok' | 'warning' | 'fail' | 'disabled';
  lastUpdate: number;
  payload?: any;
}

export interface CommLinkState {
  id: string;
  state: 'ok' | 'warning' | 'fail' | 'disabled';
  lastUpdate: number;
  payload?: any;
}

export interface FaultState {
  fault_id: string;
  type: string;
  active: boolean;
  injectedAt: number;
  duration: number;
}

export interface ControlCmdState {
  rudder: number;
  throttle: number;
  rpm: number;
  pitch: number;
  history: Array<{ rudder: number; throttle: number; rpm: number; pitch: number; t: number }>;
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
  /** Real-time 6-dim scoring row from /sil/scoring_row @ 1Hz */
  scoringRow: any;
  /** Sensor health (8 sensors) */
  sensors: SensorState[];
  /** Comm-link health (6 links) */
  commLinks: CommLinkState[];
  /** Active fault list */
  faultStatus: FaultState[];
  /** Latest control command from M5/L4 */
  controlCmd: ControlCmdState | null;
  /** Preflight log ring buffer */
  preflightLog: Array<{ timestamp: string; level: string; message: string }>;

  updateOwnShip: (state: OwnShipState) => void;
  updateTargets: (targets: TargetVesselState[]) => void;
  updateEnvironment: (env: EnvironmentState) => void;
  updateModulePulses: (pulses: ModulePulse[]) => void;
  appendAsdrEvent: (evt: ASDREvent) => void;
  updateLifecycleStatus: (status: LifecycleStatus) => void;
  setWsConnected: (v: boolean) => void;
  updateScoringRow: (row: any) => void;
  updateSensors: (sensors: SensorState[]) => void;
  updateCommLinks: (links: CommLinkState[]) => void;
  updateFaultStatus: (faults: FaultState[]) => void;
  updateControlCmd: (cmd: ControlCmdState) => void;
  appendPreflightLog: (entry: { timestamp: string; level: string; message: string }) => void;
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
  scoringRow: null,
  sensors: [],
  commLinks: [],
  faultStatus: [],
  controlCmd: null,
  preflightLog: [],
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
    // Keep ownShip null when decoded proto has no valid pose coordinates
    return {};
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
  updateScoringRow: (scoringRow) => set({ scoringRow }),
  updateSensors: (sensors) => set({ sensors }),
  updateCommLinks: (commLinks) => set({ commLinks }),
  updateFaultStatus: (faultStatus) => set({ faultStatus }),
  updateControlCmd: (controlCmd) => set((s) => {
    const history = s.controlCmd?.history ?? [];
    const entry = {
      rudder: controlCmd.rudder,
      throttle: controlCmd.throttle,
      rpm: controlCmd.rpm,
      pitch: controlCmd.pitch,
      t: Date.now(),
    };
    return {
      controlCmd: { ...controlCmd, history: [...history, entry].slice(-600) },
    };
  }),
  appendPreflightLog: (entry) => set((s) => ({
    preflightLog: [...s.preflightLog, entry].slice(-1000),
  })),
  reset: () => set(initialState),
}));
