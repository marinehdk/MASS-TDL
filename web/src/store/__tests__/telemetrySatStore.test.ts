import { describe, it, expect, beforeEach } from 'vitest';
import { useTelemetryStore } from '../telemetryStore';
import type { SAT2Data, SAT3Data, SotifMetrics } from '../../types/sat';

describe('telemetryStore SAT fields', () => {
  beforeEach(() => { useTelemetryStore.getState().reset(); });

  it('sat2 starts null', () => {
    expect(useTelemetryStore.getState().sat2).toBeNull();
  });

  it('updateSat2 stores SAT2Data', () => {
    const data: SAT2Data = {
      ivp_contributions: [{ direction_deg: 0, cost: 0.2 }, { direction_deg: 90, cost: 0.8 }],
      active_behavior: 'COLREGs_Avoidance',
      active_behavior_weight: 0.7,
      colregs_chain: [],
      colregs_chain_target_id: '123456789',
      reasoning_latency_ms: 2.3,
    };
    useTelemetryStore.getState().updateSat2(data);
    expect(useTelemetryStore.getState().sat2).toEqual(data);
  });

  it('sat3 starts null', () => {
    expect(useTelemetryStore.getState().sat3).toBeNull();
  });

  it('updateSat3 stores SAT3Data', () => {
    const data: SAT3Data = {
      trajectory_candidates: [
        { id: 0, points: [{ lon: 10.4, lat: 63.4 }], cost: 0.1, is_optimal: true, type: 'mid_mpc' },
      ],
      uncertainty_bands: false,
    };
    useTelemetryStore.getState().updateSat3(data);
    expect(useTelemetryStore.getState().sat3?.trajectory_candidates).toHaveLength(1);
  });

  it('sotifMetrics starts null', () => {
    expect(useTelemetryStore.getState().sotifMetrics).toBeNull();
  });

  it('updateSotifMetrics stores metrics', () => {
    const m: SotifMetrics = {
      ais_radar_consistency_sigma: 1.8,
      target_predictability_rms_m: 41,
      perception_coverage_pct: 95,
      colregs_parse_failures: 0,
      comm_link_rtt_ms: 120,
      checker_veto_rate_pct: 8,
    };
    useTelemetryStore.getState().updateSotifMetrics(m);
    expect(useTelemetryStore.getState().sotifMetrics?.checker_veto_rate_pct).toBe(8);
  });

  it('reset clears SAT fields', () => {
    useTelemetryStore.getState().updateSat2({
      ivp_contributions: [], active_behavior: 'Transit', active_behavior_weight: 0.3,
      colregs_chain: [], colregs_chain_target_id: null, reasoning_latency_ms: 0,
    });
    useTelemetryStore.getState().reset();
    expect(useTelemetryStore.getState().sat2).toBeNull();
  });
});
