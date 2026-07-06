import { describe, it, expect, vi } from 'vitest';
import { act, render } from '@testing-library/react';
import { useRef } from 'react';
import { AvoidanceRouteLayer, buildGeoJSON } from '../AvoidanceRouteLayer';
import { useTelemetryStore, type AvoidancePlanData } from '../../store/telemetryStore';

const addSourceMock = vi.fn();
const addLayerMock = vi.fn();
const setDataMock = vi.fn();

const mockMap = {
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  addSource: addSourceMock,
  addLayer: addLayerMock,
  getLayer: vi.fn(() => true),
  setPaintProperty: vi.fn(),
  getSource: vi.fn(() => ({ setData: setDataMock })),
};

vi.mock('maplibre-gl', () => ({ default: { Map: vi.fn(() => mockMap) } }));

const plan: AvoidancePlanData = {
  waypoints: [
    { lat: 63.4, lon: 10.4, confidence: 0.91, targetSpeedKn: 7.2, turnRadiusM: 450 },
    { lat: 63.405, lon: 10.415, confidence: 0.88, targetSpeedKn: 7.0, turnRadiusM: 420 },
  ],
  horizonS: 90,
  status: 'NORMAL',
  confidence: 0.9,
  rationale: 'M5 avoidance',
};

function Wrapper({ avoidancePlan }: { avoidancePlan: AvoidancePlanData | null }) {
  const ref = useRef(mockMap as any);
  return <AvoidanceRouteLayer mapRef={ref} avoidancePlan={avoidancePlan} visible={true} />;
}

function StoreBackedWrapper() {
  const ref = useRef(mockMap as any);
  return <AvoidanceRouteLayer mapRef={ref} visible={true} />;
}

describe('AvoidanceRouteLayer', () => {
  it('renders null', () => {
    const { container } = render(<Wrapper avoidancePlan={null} />);
    expect(container.firstChild).toBeNull();
  });

  it('adds a dashed short-term avoidance route from M5 waypoints', () => {
    addSourceMock.mockClear();
    addLayerMock.mockClear();

    render(<Wrapper avoidancePlan={plan} />);

    expect(addSourceMock).toHaveBeenCalledWith('avoidance-route', expect.objectContaining({ type: 'geojson' }));
    const data = addSourceMock.mock.calls[0][1].data;
    expect(data.features[0].geometry.coordinates).toEqual([[10.4, 63.4], [10.415, 63.405]]);

    expect(addLayerMock).toHaveBeenCalledWith(expect.objectContaining({
      id: 'avoidance-route-line',
      paint: expect.objectContaining({
        'line-dasharray': [4, 2],
      }),
    }));
    expect(addLayerMock).toHaveBeenCalledWith(expect.objectContaining({
      id: 'avoidance-route-points',
      type: 'circle',
    }));
  });

  it('renders a visible waypoint marker when M5 publishes only one avoidance waypoint', () => {
    addSourceMock.mockClear();
    addLayerMock.mockClear();

    render(<Wrapper avoidancePlan={{ ...plan, waypoints: [plan.waypoints[0]] }} />);

    const data = addSourceMock.mock.calls[0][1].data;
    expect(data.features).toContainEqual(expect.objectContaining({
      geometry: { type: 'Point', coordinates: [10.4, 63.4] },
    }));

    expect(addLayerMock).toHaveBeenCalledWith(expect.objectContaining({
      id: 'avoidance-route-points',
      type: 'circle',
      paint: expect.objectContaining({
        'circle-opacity': 0.95,
      }),
    }));
  });

  it('keeps waypoint markers when fewer than two waypoints exist', () => {
    addSourceMock.mockClear();
    setDataMock.mockClear();

    const { rerender } = render(<Wrapper avoidancePlan={plan} />);
    rerender(<Wrapper avoidancePlan={{ ...plan, waypoints: [plan.waypoints[0]] }} />);

    expect(setDataMock).toHaveBeenCalledWith(expect.objectContaining({
      features: [expect.objectContaining({
        geometry: { type: 'Point', coordinates: [10.4, 63.4] },
      })],
    }));
  });

  it('updates from telemetry store when no avoidancePlan prop is provided', () => {
    useTelemetryStore.getState().reset();
    addSourceMock.mockClear();
    setDataMock.mockClear();

    render(<StoreBackedWrapper />);

    act(() => {
      useTelemetryStore.getState().updateAvoidancePlan({
        waypoints: [
          { position: { latitude: 63.4, longitude: 10.4 } },
          { position: { latitude: 63.405, longitude: 10.415 } },
        ],
        horizon_s: 90,
        status: 'NORMAL',
      });
    });

    expect(setDataMock).toHaveBeenCalledWith(expect.objectContaining({
      features: expect.arrayContaining([expect.objectContaining({
        geometry: expect.objectContaining({
          coordinates: [[10.4, 63.4], [10.415, 63.405]],
        }),
      })]),
    }));
  });
});

it('includes segment source and audit metadata in route layer properties', () => {
  const geojson = buildGeoJSON({
    waypoints: [
      { lat: 63.44, lon: 10.38, segmentSource: 'MID_MPC_OPTIMIZED', targetSpeedMps: 3.2 },
      { lat: 63.45, lon: 10.39, segmentSource: 'REJOIN_TO_L2', targetSpeedMps: 3.0 },
      { lat: 63.46, lon: 10.40, segmentSource: 'L2_NOMINAL_SUFFIX', targetSpeedMps: 4.0 },
    ],
    status: 'RECOVERY',
    confidence: 0.82,
    rationale: 'audit visible',
    routeHash: 0x1234abcd,
    nlpSolverStatus: 3,
    nlpKktResidual: 0.125,
    nlpTailGateFailed: true,
  });

  const line = geojson.features.find((feature) => feature.geometry.type === 'LineString');
  expect(line?.properties).toMatchObject({
    segmentSources: 'MID_MPC_OPTIMIZED,REJOIN_TO_L2,L2_NOMINAL_SUFFIX',
    routeHash: 0x1234abcd,
    nlpSolverStatus: 3,
    nlpKktResidual: 0.125,
    nlpTailGateFailed: true,
  });
  const points = geojson.features.filter((feature) => feature.geometry.type === 'Point');
  expect(points[0]?.properties?.segmentSource).toBe('MID_MPC_OPTIMIZED');
  expect(points[1]?.properties?.segmentSource).toBe('REJOIN_TO_L2');
});
