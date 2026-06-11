import { beforeEach, describe, it, expect, vi } from 'vitest';
import { act, render } from '@testing-library/react';
import { appendCurrentPositionToTrail, SilMapView } from '../SilMapView';

const addLayerMock = vi.fn();

const mockMap = {
  addControl: vi.fn(),
  remove: vi.fn(),
  on: vi.fn(),
  off: vi.fn(),
  addSource: vi.fn(),
  addLayer: addLayerMock,
  getSource: vi.fn(() => ({ setData: vi.fn() })),
  getCenter: vi.fn(() => ({ lng: 10.4, lat: 63.4 })),
  getContainer: vi.fn(() => ({ clientHeight: 800 })),
  jumpTo: vi.fn(),
  easeTo: vi.fn(),
  setPadding: vi.fn(),
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  setPaintProperty: vi.fn(),
  setLayoutProperty: vi.fn(),
};

vi.mock('maplibre-gl', () => ({
  default: {
    Map: vi.fn(() => mockMap),
    NavigationControl: vi.fn(() => ({})),
    ScaleControl: vi.fn(() => ({})),
    Marker: vi.fn(() => ({ setLngLat: vi.fn().mockReturnThis(), addTo: vi.fn().mockReturnThis(), setRotation: vi.fn().mockReturnThis(), remove: vi.fn(), getElement: vi.fn(() => ({ innerHTML: '' })) })),
  },
}));

describe('SilMapView', () => {
  beforeEach(() => {
    addLayerMock.mockClear();
    mockMap.on.mockClear();
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
});
