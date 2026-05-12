import { describe, it, expect, vi } from 'vitest';
import { render } from '@testing-library/react';

vi.mock('maplibre-gl', () => ({
  default: {
    Map: vi.fn().mockReturnValue({
      addControl: vi.fn(),
      remove: vi.fn(),
    }),
    NavigationControl: vi.fn(),
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
