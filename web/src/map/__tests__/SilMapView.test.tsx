import { beforeEach, describe, it, expect, vi } from 'vitest';
import { act, render, waitFor } from '@testing-library/react';
import { appendCurrentPositionToTrail, SilMapView } from '../SilMapView';

const mocks = vi.hoisted(() => {
  const addLayerMock = vi.fn();
  const markerMock = vi.fn((options?: { element?: HTMLDivElement }) => ({
    setLngLat: vi.fn().mockReturnThis(),
    addTo: vi.fn().mockReturnThis(),
    setRotation: vi.fn().mockReturnThis(),
    remove: vi.fn(),
    getElement: vi.fn(() => options?.element ?? document.createElement('div')),
  }));
  const mockMap = {
    addControl: vi.fn(),
    remove: vi.fn(),
    on: vi.fn(),
    off: vi.fn(),
    addSource: vi.fn(),
    addLayer: addLayerMock,
    getLayer: vi.fn(),
    getSource: vi.fn(() => ({ setData: vi.fn() })),
    getCenter: vi.fn(() => ({ lng: 10.4, lat: 63.4 })),
    getContainer: vi.fn(() => ({ clientHeight: 800 })),
    jumpTo: vi.fn(),
    easeTo: vi.fn(),
    fitBounds: vi.fn(),
    setPadding: vi.fn(),
    isStyleLoaded: vi.fn(() => true),
    once: vi.fn(),
    project: vi.fn(() => ({ x: 100, y: 100 })),
    queryRenderedFeatures: vi.fn(() => []),
    resize: vi.fn(),
    setPaintProperty: vi.fn(),
    setLayoutProperty: vi.fn(),
  };

  return { addLayerMock, markerMock, mockMap };
});

const addLayerMock = mocks.addLayerMock;
const mockMap = mocks.mockMap;

vi.mock('maplibre-gl', () => ({
  default: {
    Map: vi.fn(() => mockMap),
    NavigationControl: vi.fn(() => ({})),
    ScaleControl: vi.fn(() => ({})),
    Marker: mocks.markerMock,
  },
}));

describe('SilMapView', () => {
  beforeEach(() => {
    addLayerMock.mockClear();
    mockMap.on.mockClear();
    mocks.markerMock.mockClear();
  });

  it('renders map container div', () => {
    const { getByTestId } = render(<SilMapView />);
    expect(getByTestId('sil-map-view')).toBeInTheDocument();
  });

  it('draws historical own and target trails as solid lines', () => {
    render(<SilMapView />);

    const loadHandler = mockMap.on.mock.calls.find((call) => call[0] === 'load')?.[1];
    expect(loadHandler).toBeTypeOf('function');
    act(() => {
      loadHandler();
    });

    const trailLayer = addLayerMock.mock.calls.find((call) => call[0]?.id === 'trail-line')?.[0];
    const targetTrailLayer = addLayerMock.mock.calls.find((call) => call[0]?.id === 'tgt-trail-line')?.[0];

    expect(trailLayer?.paint).not.toHaveProperty('line-dasharray');
    expect(targetTrailLayer?.paint).not.toHaveProperty('line-dasharray');
  });

  it('extends rendered trail to current vessel position without duplicating endpoint', () => {
    const trail: [number, number][] = [[10.4, 63.4], [10.41, 63.41]];

    expect(appendCurrentPositionToTrail(trail, 10.42, 63.42)).toEqual([
      [10.4, 63.4],
      [10.41, 63.41],
      [10.42, 63.42],
    ]);

    expect(appendCurrentPositionToTrail(trail, 10.41, 63.41)).toEqual(trail);
  });

  it('keeps vessel markers absolutely positioned for MapLibre anchoring', async () => {
    render(
      <SilMapView
        previewData={{
          ownShip: { lat: 63.4, lon: 10.4, heading: 30, sog: 7, cog: 30 },
          targets: [{ id: 'target-1', lat: 63.41, lon: 10.41, heading: 210, sog: 8, cog: 210 }],
        }}
      />
    );

    const loadHandler = mockMap.on.mock.calls.find((call) => call[0] === 'load')?.[1];
    expect(loadHandler).toBeTypeOf('function');
    act(() => {
      loadHandler();
    });

    await waitFor(() => expect(mocks.markerMock).toHaveBeenCalledTimes(2));
    const markerElements = mocks.markerMock.mock.calls.map(([options]) => options?.element as HTMLDivElement);

    expect(markerElements.map((el) => el.style.position)).toEqual(['absolute', 'absolute']);
  });
});
