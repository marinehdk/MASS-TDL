import { describe, it, expect, vi } from 'vitest';
import { render } from '@testing-library/react';
import { Provider } from 'react-redux';
import { configureStore } from '@reduxjs/toolkit';
import { silApi } from '../../api/silApi';
import { ScenarioBuilder } from '../ScenarioBuilder';

// Mock SilMapView since maplibre-gl needs WebGL (not available in jsdom)
vi.mock('../../map/SilMapView', () => ({
  SilMapView: () => <div data-testid="sil-map-view" />,
}));

// Mock BuilderRightRail since it's a complex UI component
vi.mock('../shared/BuilderRightRail', () => ({
  BuilderRightRail: (props: any) => <div data-testid="builder-right-rail" />,
}));

function renderWithProviders() {
  const store = configureStore({
    reducer: { [silApi.reducerPath]: silApi.reducer },
    middleware: (gdm) => gdm().concat(silApi.middleware),
  });
  return render(
    <Provider store={store}>
      <ScenarioBuilder />
    </Provider>
  );
}

describe('ScenarioBuilder', () => {
  it('renders without crashing', () => {
    const { getByTestId } = renderWithProviders();
    expect(getByTestId('scenario-builder')).toBeInTheDocument();
  });
});
