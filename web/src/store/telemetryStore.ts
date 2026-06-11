import { create } from 'zustand';
import type { OwnShipState, TargetVesselState, EnvironmentState, ModulePulse } from '../types';
import type { SAT2Data, SAT3Data, SotifMetrics } from '../types/sat';

export interface NormalizedWaypoint {
  lat: number;
  lon: number;
}

export interface VoyagePlanData {
  waypoints: NormalizedWaypoint[];
  cruiseSpeed: number;
  source: 'static_yaml' | 'l2_realtime';
}

export interface AvoidanceWaypointData extends NormalizedWaypoint {
  confidence?: number;
  targetSpeedKn?: number;
  turnRadiusM?: number;
  rationale?: string;
}

export interface AvoidancePlanData {
  waypoints: AvoidanceWaypointData[];
  horizonS?: number;
  status?: string;
  confidence?: number;
  rationale?: string;
}

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

const STALE_MS = 3_000;

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
  lastTrailTime: number;
  /** [lon, lat] pairs for target ship trajectory trails, keyed by target ID/MMSI */
  targetTrails: Record<string, [number, number][]>;
  targetLastTrailTimes: Record<string, number>;
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
  sat2: SAT2Data | null;
  sat3: SAT3Data | null;
  sotifMetrics: SotifMetrics | null;
  sat2LastReceivedAt: number | null;
  sat3LastReceivedAt: number | null;
  sotifMetricsLastReceivedAt: number | null;
  isSat2Stale: () => boolean;
  isSat3Stale: () => boolean;
  isSotifMetricsStale: () => boolean;
  voyagePlan: VoyagePlanData | null;
  avoidancePlan: AvoidancePlanData | null;

  updateOwnShip: (state: OwnShipState) => void;
  updateTargets: (targets: TargetVesselState[]) => void;
  clearTargets: () => void;
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
  updateSat2: (data: SAT2Data) => void;
  updateSat3: (data: SAT3Data) => void;
  updateSotifMetrics: (metrics: SotifMetrics) => void;
  updateVoyagePlan: (plan: VoyagePlanData | null) => void;
  updateAvoidancePlan: (plan: any | null) => void;
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
  targetTrails: {} as Record<string, [number, number][]>,
  targetLastTrailTimes: {} as Record<string, number>,
  scoringRow: null,
  sensors: [],
  commLinks: [],
  faultStatus: [],
  controlCmd: null,
  preflightLog: [],
  sat2: null,
  sat3: null,
  sotifMetrics: null,
  sat2LastReceivedAt: null,
  sat3LastReceivedAt: null,
  sotifMetricsLastReceivedAt: null,
  lastTrailTime: 0,
  voyagePlan: null,
  avoidancePlan: null,
};

function numberOrUndefined(value: unknown): number | undefined {
  return typeof value === 'number' && Number.isFinite(value) ? value : undefined;
}

function normalizeAvoidancePlan(plan: any | null): AvoidancePlanData | null {
  if (!plan) return null;

  const waypoints = Array.isArray(plan.waypoints)
    ? plan.waypoints.flatMap((wp: any) => {
      const pos = wp?.position ?? wp;
      const lat = numberOrUndefined(pos?.latitude ?? pos?.lat);
      const lon = numberOrUndefined(pos?.longitude ?? pos?.lon);
      if (lat === undefined || lon === undefined) return [];

      return [{
        lat,
        lon,
        confidence: numberOrUndefined(wp?.confidence),
        targetSpeedKn: numberOrUndefined(wp?.target_speed_kn ?? wp?.targetSpeedKn),
        turnRadiusM: numberOrUndefined(wp?.turn_radius_m ?? wp?.turnRadiusM),
        rationale: typeof wp?.rationale === 'string' ? wp.rationale : undefined,
      }];
    })
    : [];

  return {
    waypoints,
    horizonS: numberOrUndefined(plan.horizon_s ?? plan.horizonS),
    status: typeof plan.status === 'string' ? plan.status : undefined,
    confidence: numberOrUndefined(plan.confidence),
    rationale: typeof plan.rationale === 'string' ? plan.rationale : undefined,
  };
}

export const useTelemetryStore = create<TelemetryState>((set, get) => ({
  ...initialState,
  isSat2Stale: () => {
    const t = get().sat2LastReceivedAt;
    return t === null || Date.now() - t > STALE_MS;
  },
  isSat3Stale: () => {
    const t = get().sat3LastReceivedAt;
    return t === null || Date.now() - t > STALE_MS;
  },
  isSotifMetricsStale: () => {
    const t = get().sotifMetricsLastReceivedAt;
    return t === null || Date.now() - t > STALE_MS;
  },
  updateOwnShip: (ownShip) => set((s) => {
    // Append to trail if coordinates are valid
    const lon = ownShip.pose?.lon;
    const lat = ownShip.pose?.lat;
    if (typeof lon === 'number' && typeof lat === 'number') {
      const now = Date.now();
      if (s.ownShipTrail.length === 0 || now - s.lastTrailTime >= 1000) {
        const trail = [...s.ownShipTrail, [lon, lat] as [number, number]];
        return {
          ownShip,
          ownShipTrail: trail.length > MAX_TRAIL ? trail.slice(-MAX_TRAIL) : trail,
          lastTrailTime: now,
        };
      }
      return { ownShip };
    }
    // Keep ownShip null when decoded proto has no valid pose coordinates
    return {};
  }),
  updateTargets: (newTargets) => set((s) => {
    const merged = [...s.targets];
    const trails = { ...s.targetTrails };
    const lastTrailTimes = { ...s.targetLastTrailTimes };
    const now = Date.now();

    for (const nt of newTargets) {
      const idx = merged.findIndex((t) => t.mmsi === nt.mmsi);
      if (idx >= 0) {
        merged[idx] = nt;
      } else {
        merged.push(nt);
      }

      const id = nt.mmsi != null ? String(nt.mmsi) : null;
      if (id) {
        const lon = nt.pose?.lon;
        const lat = nt.pose?.lat;
        if (typeof lon === 'number' && typeof lat === 'number') {
          const lastTime = lastTrailTimes[id] ?? 0;
          if (!trails[id] || trails[id].length === 0 || now - lastTime >= 1000) {
            const currentTrail = trails[id] ?? [];
            const newTrail = [...currentTrail, [lon, lat] as [number, number]];
            trails[id] = newTrail.length > MAX_TRAIL ? newTrail.slice(-MAX_TRAIL) : newTrail;
            lastTrailTimes[id] = now;
          }
        }
      }
    }
    return {
      targets: merged,
      targetTrails: trails,
      targetLastTrailTimes: lastTrailTimes,
    };
  }),
  clearTargets: () => set({
    targets: [],
    targetTrails: {},
    targetLastTrailTimes: {},
  }),
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
  updateSat2: (sat2) => set({ sat2, sat2LastReceivedAt: Date.now() }),
  updateSat3: (sat3) => set({ sat3, sat3LastReceivedAt: Date.now() }),
  updateSotifMetrics: (sotifMetrics) => set({ sotifMetrics, sotifMetricsLastReceivedAt: Date.now() }),
  updateVoyagePlan: (voyagePlan) => set({ voyagePlan }),
  updateAvoidancePlan: (plan) => set({ avoidancePlan: normalizeAvoidancePlan(plan) }),
  reset: () => set(initialState),
}));

if (import.meta.env.DEV) {
  (window as any).__TELEMETRY_STORE__ = useTelemetryStore;
}
