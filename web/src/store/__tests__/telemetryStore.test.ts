import { describe, it, expect, beforeEach } from 'vitest';
import { useTelemetryStore } from '../telemetryStore';

describe('telemetryStore', () => {
  beforeEach(() => useTelemetryStore.getState().reset());

  it('starts with null ownShip', () => {
    expect(useTelemetryStore.getState().ownShip).toBeNull();
  });

  it('starts with empty targets array', () => {
    expect(useTelemetryStore.getState().targets).toEqual([]);
  });

  it('starts with null avoidancePlan', () => {
    expect(useTelemetryStore.getState().avoidancePlan).toBeNull();
  });

  it('normalizes M1/M4/M6/M7 decision telemetry', () => {
    useTelemetryStore.getState().updateOddState({
      current_zone: 1,
      auto_level: 3,
      health: 1,
      envelope_state: 1,
      conformance_score: 0.88,
      confidence: 0.93,
      rationale: 'coastal channel',
    } as any);
    useTelemetryStore.getState().updateBehaviorPlan({
      behavior: 1,
      heading_min_deg: 15,
      heading_max_deg: 35,
      speed_min_kn: 6,
      speed_max_kn: 9,
      confidence: 0.81,
      rationale: 'COLREG give-way window',
    } as any);
    useTelemetryStore.getState().updateColregsConstraint({
      active_rules: [{
        rule_id: 14,
        role: 1,
        preferred_direction: 'STARBOARD',
        min_alteration_deg: 22,
      }],
      phase: 'T_act',
      primary_role: 1,
      primary_preferred_direction: 'STARBOARD',
      confidence: 0.89,
      rationale: 'head-on target',
    } as any);
    useTelemetryStore.getState().updateSafetyAlert({
      alert_type: 1,
      severity: 2,
      description: 'SOTIF assumption breach',
      recommended_mrm: 'MRM-02',
      confidence: 0.75,
      rationale: 'checker active',
    } as any);

    expect(useTelemetryStore.getState().oddState).toEqual(expect.objectContaining({
      envelopeState: 1,
      zone: 1,
      health: 1,
      confidence: 0.93,
    }));
    expect(useTelemetryStore.getState().behaviorPlan).toEqual(expect.objectContaining({
      behavior: 1,
      headingMinDeg: 15,
      headingMaxDeg: 35,
      speedMinKn: 6,
      speedMaxKn: 9,
    }));
    expect(useTelemetryStore.getState().colregsConstraint).toEqual(expect.objectContaining({
      ruleId: 14,
      role: 1,
      preferredDirection: 'STARBOARD',
      minAlterationDeg: 22,
    }));
    expect(useTelemetryStore.getState().safetyAlert).toEqual(expect.objectContaining({
      severity: 2,
      alertType: 1,
      recommendedMrm: 'MRM-02',
    }));
  });

  it('selects actionable encounter COLREGs rule ahead of Rule 5/7 obligations', () => {
    useTelemetryStore.getState().updateColregsConstraint({
      active_rules: [
        { rule_id: 5, role: 3, preferred_direction: 'HOLD', min_alteration_deg: 0 },
        { rule_id: 7, role: 3, preferred_direction: 'HOLD', min_alteration_deg: 0 },
        { rule_id: 8, role: 3, preferred_direction: 'STARBOARD', min_alteration_deg: 15 },
        { rule_id: 14, role: 2, preferred_direction: 'STARBOARD', min_alteration_deg: 15 },
        { rule_id: 16, role: 1, preferred_direction: 'STARBOARD', min_alteration_deg: 15 },
      ],
      primary_role: 1,
      primary_preferred_direction: 'STARBOARD',
      phase: 'SOUND_WARNING',
    } as any);

    expect(useTelemetryStore.getState().colregsConstraint).toEqual(expect.objectContaining({
      ruleId: 14,
      role: 1,
      preferredDirection: 'STARBOARD',
      minAlterationDeg: 15,
    }));
  });

  it('updates ownShip state', () => {
    const fake = { pose: { lat: 63.4, lon: 10.4, heading: 0.5 } } as any;
    useTelemetryStore.getState().updateOwnShip(fake);
    expect(useTelemetryStore.getState().ownShip!.pose!.lat).toBe(63.4);
  });

  it('reset clears all fields', () => {
    useTelemetryStore.getState().updateOwnShip({} as any);
    useTelemetryStore.getState().updateThreatState({
      target_ids: ['100000001'],
      risk_phases: ['Warning'],
      risk_scores: [0.42],
      primary_flags: [true],
      danger_forward_m: 500,
      warning_forward_m: 900,
    } as any);
    useTelemetryStore.getState().reset();
    expect(useTelemetryStore.getState().ownShip).toBeNull();
    expect(useTelemetryStore.getState().targets).toEqual([]);
    expect(useTelemetryStore.getState().targetTrails).toEqual({});
    expect(useTelemetryStore.getState().threatState).toBeNull();
    expect(useTelemetryStore.getState().threatRiskHistory).toEqual([]);
  });

  it('normalizes backend threat risk state and appends risk history', () => {
    useTelemetryStore.getState().updateThreatState({
      stamp: { sec: 12, nanosec: 500_000_000 },
      target_ids: ['100000001', '100000002'],
      risk_phases: ['Critical', 'Monitor'],
      risk_scores: [0.91, 0.24],
      primary_flags: [true, false],
      range_m: [640, 1800],
      dcpa_m: [180, 900],
      tcpa_s: [52, 410],
      warning_margin_m: [-120, 80],
      danger_margin_m: [-20, 520],
      closing_speed_mps: [3.1, 0.8],
      relative_bearing_deg: [12, -48],
      colregs_duties: ['BothGiveWay', 'Free'],
      tdv_warning_s: [0, 100],
      tdv_danger_s: [0, 0],
      danger_forward_m: 520,
      danger_astern_m: 150,
      danger_starboard_m: 260,
      danger_port_m: 220,
      warning_forward_m: 936,
      warning_astern_m: 270,
      warning_starboard_m: 468,
      warning_port_m: 396,
      superellipse_power: 2.5,
      rationale: 'backend risk model',
    } as any);

    const state = useTelemetryStore.getState();
    expect(state.threatState?.targets).toEqual([
      expect.objectContaining({ targetId: '100000001', riskPhase: 'Critical', riskScore: 0.91, primary: true }),
      expect.objectContaining({ targetId: '100000002', riskPhase: 'Monitor', riskScore: 0.24, primary: false }),
    ]);
    expect(state.threatState?.dangerAxes.forwardM).toBe(520);
    expect(state.threatState?.warningAxes.portM).toBe(396);
    expect(state.threatRiskHistory).toEqual([
      expect.objectContaining({ t: 12.5, primaryTargetId: '100000001', riskScore: 0.91, riskPhase: 'Critical' }),
    ]);
  });

  it('normalizes M5 avoidance plan waypoints', () => {
    useTelemetryStore.getState().updateAvoidancePlan({
      waypoints: [
        {
          position: { latitude: 63.4, longitude: 10.4 },
          confidence: 0.9,
          target_speed_kn: 7.5,
          turn_radius_m: 300,
          rationale: 'first',
        },
        {
          position: { lat: 63.41, lon: 10.42 },
          confidence: 0.8,
          targetSpeedKn: 7.0,
          turnRadiusM: 250,
        },
      ],
      horizon_s: 90,
      status: 'NORMAL',
      confidence: 0.91,
      rationale: 'M5 route',
    } as any);

    expect(useTelemetryStore.getState().avoidancePlan).toEqual({
      waypoints: [
        { lat: 63.4, lon: 10.4, confidence: 0.9, targetSpeedKn: 7.5, turnRadiusM: 300, rationale: 'first' },
        { lat: 63.41, lon: 10.42, confidence: 0.8, targetSpeedKn: 7.0, turnRadiusM: 250, rationale: undefined },
      ],
      horizonS: 90,
      status: 'NORMAL',
      confidence: 0.91,
      rationale: 'M5 route',
    });
  });

  it('starts with empty targetTrails and targetLastTrailTimes', () => {
    expect(useTelemetryStore.getState().targetTrails).toEqual({});
    expect(useTelemetryStore.getState().targetLastTrailTimes).toEqual({});
  });

  it('updateTargets records and rate-limits target trails', async () => {
    const fakeTarget1 = { mmsi: 123456, pose: { lat: 63.4, lon: 10.4, heading: 0.5 }, kinematics: { sog: 5, cog: 0.5, rot: 0 } } as any;

    // 1st update: should record the trail point
    useTelemetryStore.getState().updateTargets([fakeTarget1]);
    expect(useTelemetryStore.getState().targetTrails['123456']).toEqual([[10.4, 63.4]]);
    const initialTime = useTelemetryStore.getState().targetLastTrailTimes['123456'];
    expect(initialTime).toBeGreaterThan(0);

    // 2nd update immediate: should be rate-limited (not recorded)
    const fakeTarget2 = { mmsi: 123456, pose: { lat: 63.5, lon: 10.5, heading: 0.5 }, kinematics: { sog: 5, cog: 0.5, rot: 0 } } as any;
    useTelemetryStore.getState().updateTargets([fakeTarget2]);
    expect(useTelemetryStore.getState().targetTrails['123456']).toEqual([[10.4, 63.4]]);

    // Mock time passage of 1.1s
    const originalNow = Date.now;
    Date.now = () => initialTime + 1100;
    
    // 3rd update after 1.1s: should be recorded
    useTelemetryStore.getState().updateTargets([fakeTarget2]);
    expect(useTelemetryStore.getState().targetTrails['123456']).toEqual([[10.4, 63.4], [10.5, 63.5]]);
    
    // Restore Date.now
    Date.now = originalNow;
  });

  it('updateTargets preserves M2 CPA/TCPA fields across SIL target refreshes', () => {
    useTelemetryStore.getState().updateTargets([{
      mmsi: 100000001,
      pose: { lat: 63.4, lon: 10.4, heading: 0.5 },
      kinematics: { sog: 5, cog: 0.5, rot: 0 },
      cpaM: 2222.9,
      tcpaS: 120.0,
      brgDeg: 344.5,
      rngM: 1018.6,
    } as any]);

    useTelemetryStore.getState().updateTargets([{
      mmsi: 100000001,
      pose: { lat: 63.41, lon: 10.41, heading: 0.6 },
      kinematics: { sog: 6, cog: 0.6, rot: 0 },
    } as any]);

    expect(useTelemetryStore.getState().targets[0]).toEqual(expect.objectContaining({
      cpaM: 2222.9,
      tcpaS: 120.0,
      brgDeg: 344.5,
      rngM: 1018.6,
    }));
    expect(useTelemetryStore.getState().targets[0].pose?.lat).toBe(63.41);
  });

  it('clearTargets removes cached targets and target trails', () => {
    const fakeTarget = { mmsi: 123456, pose: { lat: 63.4, lon: 10.4, heading: 0.5 }, kinematics: { sog: 5, cog: 0.5, rot: 0 } } as any;

    useTelemetryStore.getState().updateTargets([fakeTarget]);
    useTelemetryStore.getState().clearTargets();

    expect(useTelemetryStore.getState().targets).toEqual([]);
    expect(useTelemetryStore.getState().targetTrails).toEqual({});
    expect(useTelemetryStore.getState().targetLastTrailTimes).toEqual({});
  });
});
