import { describe, expect, it } from 'vitest';
import { deriveAvoidancePhaseState, type AvoidancePhaseInput } from '../avoidancePhase';

function makeInput(overrides: Partial<AvoidancePhaseInput> = {}): AvoidancePhaseInput {
  return {
    simTimeSec: 42,
    targets: [],
    oddState: null,
    colregsConstraint: null,
    behaviorPlan: null,
    avoidancePlan: null,
    safetyAlert: null,
    sat2: null,
    sat3: null,
    sotifMetrics: null,
    previousPhase: null,
    ...overrides,
  };
}

describe('deriveAvoidancePhaseState', () => {
  it('classifies distant target as TRANSIT_DISCOVERY', () => {
    const result = deriveAvoidancePhaseState(makeInput({
      targets: [{ mmsi: 'T01', cpaM: 3704, tcpaS: 1800, rngM: 6000, brgDeg: 50 }],
    }));

    expect(result.phase).toBe('TRANSIT_DISCOVERY');
    expect(result.phaseLabel).toBe('自由航行与目标发现');
    expect(result.activeModules).toContain('M2');
    expect(result.chain.m2.cpaNm).toBeCloseTo(2.0);
    expect(result.events).toHaveLength(0);
  });

  it('classifies active COLREGs rule as RISK_RULE_ASSESSED', () => {
    const result = deriveAvoidancePhaseState(makeInput({
      targets: [{ mmsi: 'T01', cpaM: 1200, tcpaS: 360, rngM: 2500, brgDeg: 48 }],
      colregsConstraint: { ruleId: '15', role: 'GIVE_WAY', preferredDirection: 'STARBOARD', phase: 'T_avoid' },
    }));

    expect(result.phase).toBe('RISK_RULE_ASSESSED');
    expect(result.activeModules).toContain('M6');
    expect(result.chain.m6.rule).toBe('15');
    expect(result.events).toEqual(expect.arrayContaining([
      expect.objectContaining({ k: 'M6_RULE_ASSERTED', m: 'M6', sev: 'warn' }),
    ]));
  });

  it('marks close CPA/TCPA rule event as critical', () => {
    const result = deriveAvoidancePhaseState(makeInput({
      targets: [{ mmsi: 'T01', cpaM: 400, tcpaS: 240, rngM: 1600, brgDeg: 35 }],
    }));

    expect(result.phase).toBe('RISK_RULE_ASSESSED');
    expect(result.events[0]).toMatchObject({ k: 'M6_RULE_ASSERTED', sev: 'crit' });
  });

  it('classifies non-transit behavior and avoidance plan as ARBITRATION_MANEUVERING', () => {
    const result = deriveAvoidancePhaseState(makeInput({
      behaviorPlan: { behavior: 'AVOIDANCE', headingMinDeg: 20, headingMaxDeg: 45 },
      avoidancePlan: { status: 'SOLVED', horizonS: 120, waypoints: [{}, {}] },
    }));

    expect(result.phase).toBe('ARBITRATION_MANEUVERING');
    expect(result.activeModules).toEqual(expect.arrayContaining(['M4', 'M5']));
    expect(result.chain.m4.headingWindow).toBe('20.0-45.0°');
    expect(result.chain.m5.waypointCount).toBe(2);
    expect(result.events).toEqual(expect.arrayContaining([
      expect.objectContaining({ k: 'M4_MANEUVER_START', m: 'M4' }),
      expect.objectContaining({ k: 'M5_PLAN_READY', m: 'M5' }),
    ]));
  });

  it('classifies SOTIF violation as SAFETY_MONITORING', () => {
    const result = deriveAvoidancePhaseState(makeInput({
      safetyAlert: { severity: 'CRITICAL', recommendedMrm: 'MRM-03', description: 'checker veto' },
      sotifMetrics: {
        ais_radar_consistency_sigma: 1.0,
        target_predictability_rms_m: 10,
        perception_coverage_pct: 95,
        colregs_parse_failures: 0,
        comm_link_rtt_ms: 50,
        checker_veto_rate_pct: 25,
      },
    }));

    expect(result.phase).toBe('SAFETY_MONITORING');
    expect(result.activeModules).toContain('M7');
    expect(result.chain.m7.violatedMetricCount).toBe(1);
    expect(result.events).toEqual(expect.arrayContaining([
      expect.objectContaining({ k: 'M7_SAFETY_ALERT', sev: 'crit' }),
    ]));
  });

  it('classifies cleared previous encounter as CLEAR_RETURN', () => {
    const result = deriveAvoidancePhaseState(makeInput({
      previousPhase: 'ARBITRATION_MANEUVERING',
      targets: [{ mmsi: 'T01', cpaM: 2500, tcpaS: -60, rngM: 4200, brgDeg: 160 }],
      behaviorPlan: { behavior: 'TRANSIT' },
      colregsConstraint: { ruleId: null, role: null, preferredDirection: null, phase: null },
    }));

    expect(result.phase).toBe('CLEAR_RETURN');
    expect(result.activeModules).toEqual(expect.arrayContaining(['M4', 'M5', 'M8']));
    expect(result.events).toEqual(expect.arrayContaining([
      expect.objectContaining({ k: 'CLEAR_RETURN', sev: 'info' }),
    ]));
  });

  it('summarizes SAT3 optimal trajectory cost', () => {
    const result = deriveAvoidancePhaseState(makeInput({
      sat3: {
        uncertainty_bands: false,
        trajectory_candidates: [
          { id: 1, points: [], cost: 0.7, is_optimal: false, type: 'mid_mpc' },
          { id: 2, points: [], cost: 0.2, is_optimal: true, type: 'bc_mpc' },
        ],
      },
    }));

    expect(result.phase).toBe('ARBITRATION_MANEUVERING');
    expect(result.chain.m5.candidateCount).toBe(2);
    expect(result.chain.m5.optimalCandidateCost).toBe(0.2);
  });
});
