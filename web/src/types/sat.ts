// SAT-1/2/3 transparency types (Chen et al. 2014 [W54])
// Consumed by engineer-view panels; all fields optional to handle partial telemetry

export interface IvpContribution {
  direction_deg: number;    // 0 / 45 / 90 / 135 / 180 / 225 / 270 / 315
  cost: number;             // 0.0–1.0  (higher = more dangerous/costly)
  label?: string;           // debug label, e.g. 'cpa_penalty'
}

export interface ColregsChainLayer {
  layer: 1 | 2 | 3 | 4 | 5;
  label: string;            // e.g. 'ODD', '会遇分类', '责任', '方向', '时机'
  conclusion: string;       // e.g. 'GIVE-WAY', 'Rule 14', 'STBD ≥30°'
  inputs: Record<string, string | number>;
  confidence?: number;
  timing_stage?: 'STAGE_1' | 'STAGE_2' | 'STAGE_3' | 'EMERGENCY';
  escalation?: boolean;
}

export interface TrajectoryCandidate {
  id: number;
  points: Array<{ lon: number; lat: number }>;
  cost: number;             // 0.0–1.0 (lower = better)
  is_optimal: boolean;
  type: 'mid_mpc' | 'bc_mpc';
}

export interface SAT2Data {
  ivp_contributions: IvpContribution[];       // M4: 8 directional costs
  active_behavior: string | null;             // M4: winning behavior name
  active_behavior_weight: number;             // M4: weight 0.0–1.0
  colregs_chain: ColregsChainLayer[];         // M6: up to 5 layers
  colregs_chain_target_id: string | null;     // M6: MMSI of active target
  reasoning_latency_ms: number;              // M6: solve time
}

export interface SAT3Data {
  trajectory_candidates: TrajectoryCandidate[]; // M5: mid + bc candidates
  uncertainty_bands: boolean;                   // Phase 3 flag
}

export interface SotifMetrics {
  ais_radar_consistency_sigma: number;   // >2.0σ → warning
  target_predictability_rms_m: number;  // >50m  → warning
  perception_coverage_pct: number;      // <80%  → warning
  colregs_parse_failures: number;       // >3    → warning (3-window)
  comm_link_rtt_ms: number;             // >2000 → warning
  checker_veto_rate_pct: number;        // >20%  → warning (15s window)
}
