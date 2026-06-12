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

  it('updates ownShip state', () => {
    const fake = { pose: { lat: 63.4, lon: 10.4, heading: 0.5 } } as any;
    useTelemetryStore.getState().updateOwnShip(fake);
    expect(useTelemetryStore.getState().ownShip!.pose!.lat).toBe(63.4);
  });

  it('reset clears all fields', () => {
    useTelemetryStore.getState().updateOwnShip({} as any);
    useTelemetryStore.getState().reset();
    expect(useTelemetryStore.getState().ownShip).toBeNull();
    expect(useTelemetryStore.getState().targets).toEqual([]);
    expect(useTelemetryStore.getState().targetTrails).toEqual({});
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

  it('clearTargets removes cached targets and target trails', () => {
    const fakeTarget = { mmsi: 123456, pose: { lat: 63.4, lon: 10.4, heading: 0.5 }, kinematics: { sog: 5, cog: 0.5, rot: 0 } } as any;

    useTelemetryStore.getState().updateTargets([fakeTarget]);
    useTelemetryStore.getState().clearTargets();

    expect(useTelemetryStore.getState().targets).toEqual([]);
    expect(useTelemetryStore.getState().targetTrails).toEqual({});
    expect(useTelemetryStore.getState().targetLastTrailTimes).toEqual({});
  });
});
