import { describe, it, expect, vi } from 'vitest';
import { render } from '@testing-library/react';

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
import { Provider } from 'react-redux';
import { configureStore } from '@reduxjs/toolkit';
import { silApi } from '../../api/silApi';
import { BridgeHMI } from '../BridgeHMI';

function renderWithProviders() {
  const store = configureStore({
    reducer: { [silApi.reducerPath]: silApi.reducer },
    middleware: (gdm) => gdm().concat(silApi.middleware),
  });
  return render(
    <Provider store={store}>
      <BridgeHMI />
    </Provider>
  );
}

describe('BridgeHMI', () => {
  it('renders without crashing', () => {
    const { getByTestId } = renderWithProviders();
    expect(getByTestId('bridge-hmi')).toBeInTheDocument();
  });
});
