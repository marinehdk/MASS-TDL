import { describe, it, expect, vi } from 'vitest';
import { render } from '@testing-library/react';
import { SilMapView } from '../SilMapView';

const mockMap = {
  addControl: vi.fn(),
  remove: vi.fn(),
  on: vi.fn((event: string, cb: () => void) => { if (event === 'load') cb(); }),
  addSource: vi.fn(),
  addLayer: vi.fn(),
  getSource: vi.fn(() => ({ setData: vi.fn() })),
  getCenter: vi.fn(() => ({ lng: 10.4, lat: 63.4 })),
  jumpTo: vi.fn(),
  easeTo: vi.fn(),
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
  it('renders map container div', () => {
    const { getByTestId } = render(<SilMapView />);
    expect(getByTestId('sil-map-view')).toBeInTheDocument();
  });
});
