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
  });
});
