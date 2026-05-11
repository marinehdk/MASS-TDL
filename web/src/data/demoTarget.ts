import type { NavState, TargetState } from '../types/sil';

/** DEMO-1 R14 head-on encounter: target ship ~2.5nm north of own-ship */
export const DEMO_R14_TARGET: TargetState = {
  mmsi: 'DEMO001',
  lat: 63.458,
  lon: 10.395,
  sog_kn: 10.0,
  cog_deg: 180,
  heading_deg: 180,
  cpa_nm: 0.18,
  tcpa_s: 520,
  colreg_role: 'give-way',
  confidence: 1.0,
};

/** DEMO-1 own-ship initial state (FCB in Trondheim Fjord) */
export const DEMO_OWN_SHIP: NavState = {
  lat: 63.435,
  lon: 10.395,
  sog_kn: 18.0,
  cog_deg: 0,
  heading_deg: 5,
};

/** DEMO-1 mock SilDebugData for side panel when /ws/sil_debug is not connected */
export function makeDemoSilData() {
  return {
    timestamp: new Date().toISOString(),
    scenario_id: 'r14-ho-demo',
    sat1: null as any,
    sat2_reasoning: null,
    sat3_tdl_s: 184,
    sat3_tmr_s: 60,
    odd: { zone: 'A · OPEN WATER', envelope_state: 'IN', conformance_score: 0.87, confidence: 0.9 },
    job_status: 'idle' as const,
    cpa_nm: 0.18,
    tcpa_s: 520,
    rule_text: 'R14 Head-on · Both give way',
    decision_text: 'M5 BC-MPC: right turn 10° · Δheading +12°',
    module_status: [true, true, false, true, false, true, false, false],
    asdr_events: [
      { timestamp: '14:32:01', event_type: 'step', step: 0, rule: 'R14', cpa_nm: 0.08, tcpa_s: 580, action: 'M2 detecting collision risk', verdict: 'RISK' as const },
      { timestamp: '14:32:03', event_type: 'step', step: 1, rule: 'R14', cpa_nm: 0.12, tcpa_s: 520, action: 'M6 classifying: both turn starboard', verdict: 'RISK' as const },
    ],
  };
}
