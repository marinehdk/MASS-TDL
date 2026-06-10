import React from 'react';
import { cleanup, render } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { PlannedRouteLayer, buildWaypointRouteMetrics } from '../PlannedRouteLayer';

afterEach(() => {
  cleanup();
});

function createMapMock() {
  const sources = new Map<string, { setData: ReturnType<typeof vi.fn> }>();
  const layers = new Set<string>();
  const map = {
    addSource: vi.fn((id: string) => {
      sources.set(id, { setData: vi.fn() });
    }),
    addLayer: vi.fn((layer: { id: string }) => {
      layers.add(layer.id);
    }),
    getSource: vi.fn((id: string) => sources.get(id)),
    getLayer: vi.fn((id: string) => layers.has(id) ? { id } : undefined),
    getStyle: vi.fn(() => ({ layers: [{ id: 'base' }] })),
    setPaintProperty: vi.fn(),
    on: vi.fn(),
    off: vi.fn(),
    once: vi.fn(),
    getCanvas: vi.fn(() => ({ style: { cursor: '' } })),
    sources,
  };
  return map;
}

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

describe('PlannedRouteLayer', () => {
  it('adds and updates route line and waypoint sources without segment label source or layer', () => {
    const map = createMapMock();
    const mapRef = { current: map as any };
    const initialWaypoints = [
      { lat: 0, lon: 0 },
      { lat: 0, lon: 1 },
    ];

    const { rerender } = render(React.createElement(PlannedRouteLayer, {
      mapRef,
      waypoints: initialWaypoints,
      visible: true,
    }));

    expect(map.addSource).toHaveBeenCalledWith('planned-route-src', expect.any(Object));
    expect(map.addSource).toHaveBeenCalledWith('planned-route-wp-src', expect.any(Object));
    expect(map.addSource).not.toHaveBeenCalledWith('planned-route-lbl-src', expect.any(Object));
    expect(map.addLayer).not.toHaveBeenCalledWith(expect.objectContaining({
      id: 'planned-route-lbl-symbol',
    }));

    rerender(React.createElement(PlannedRouteLayer, {
      mapRef,
      waypoints: [
        { lat: 0, lon: 0 },
        { lat: 0, lon: 2 },
        { lat: 1, lon: 2 },
      ],
      visible: false,
    }));

    expect(map.sources.get('planned-route-src')?.setData).toHaveBeenCalledTimes(1);
    expect(map.sources.get('planned-route-wp-src')?.setData).toHaveBeenCalledTimes(1);
    expect(map.getSource).not.toHaveBeenCalledWith('planned-route-lbl-src');
    expect(map.setPaintProperty).toHaveBeenCalledWith('planned-route-line', 'line-opacity', 0);
    expect(map.setPaintProperty).toHaveBeenCalledWith('planned-route-wp-circle', 'circle-opacity', 0);
    expect(map.setPaintProperty).not.toHaveBeenCalledWith('planned-route-lbl-symbol', expect.any(String), expect.anything());
  });
});
