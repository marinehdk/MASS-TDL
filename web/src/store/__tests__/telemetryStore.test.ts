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
});
