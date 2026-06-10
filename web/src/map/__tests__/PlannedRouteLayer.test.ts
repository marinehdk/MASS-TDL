import { describe, expect, it } from 'vitest';

import { buildWaypointRouteMetrics } from '../PlannedRouteLayer';

describe('buildWaypointRouteMetrics', () => {
  it('reports total length, waypoint number, next segment and turn angle', () => {
    const metrics = buildWaypointRouteMetrics([
      { lat: 0, lon: 0 },
      { lat: 0, lon: 1 },
      { lat: 1, lon: 1 },
    ], 1);

    expect(metrics).toMatchObject({
      waypointNumber: 2,
      waypointCount: 3,
    });
    expect(metrics.totalDistanceNm).toBeCloseTo(120.1, 1);
    expect(metrics.nextSegmentNm).toBeCloseTo(60.0, 1);
    expect(metrics.turnAngleDeg).toBeCloseTo(90.0, 1);
  });

  it('marks missing next segment or previous segment as unavailable', () => {
    const waypoints = [
      { lat: 0, lon: 0 },
      { lat: 0, lon: 1 },
    ];

    expect(buildWaypointRouteMetrics(waypoints, 0)).toMatchObject({
      waypointNumber: 1,
      nextSegmentNm: expect.any(Number),
      turnAngleDeg: null,
    });
    expect(buildWaypointRouteMetrics(waypoints, 1)).toMatchObject({
      waypointNumber: 2,
      nextSegmentNm: null,
      turnAngleDeg: null,
    });
  });
});
