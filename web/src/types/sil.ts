/** Matches SilDebugSchema from sil_schemas.py */
export interface SilDebugData {
  timestamp: string;
  scenario_id: string;
  sat1: {
    threat_count: number;
    threats: Sat1Threat[];
  } | null;
  sat2_reasoning: string | null;
  sat3_tdl_s: number;
  sat3_tmr_s: number;
  odd: {
    zone: string;
    envelope_state: string;
    conformance_score: number;
    confidence: number;
  } | null;
  job_status: 'idle' | 'running' | 'done';
  cpa_nm: number;
  tcpa_s: number;
  rule_text: string;
  decision_text: string;
  module_status: boolean[];
  asdr_events: AsdrEvent[];
}

export interface Sat1Threat {
  id: string;
  bearing_deg: number;
  range_nm: number;
  cpa_nm: number;
  tcpa_s: number;
}

export interface AsdrEvent {
  timestamp: string;
  event_type: string;
  step: number;
  rule: string;
  cpa_nm: number;
  tcpa_s: number;
  action: string;
  verdict: 'PASS' | 'RISK';
}

export interface NavState {
  lat: number;
  lon: number;
  sog_kn: number;
  cog_deg: number;
  heading_deg: number;
}

export interface TargetState {
  mmsi: string;
  lat: number;
  lon: number;
  sog_kn: number;
  cog_deg: number;
  heading_deg: number;
  cpa_nm: number;
  tcpa_s: number;
  colreg_role: 'give-way' | 'stand-on' | 'overtaking' | 'safe';
  confidence: number;
}
