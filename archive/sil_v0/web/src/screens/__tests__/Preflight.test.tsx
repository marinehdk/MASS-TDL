import { describe, it, expect } from 'vitest';
import { render } from '@testing-library/react';
import { Provider } from 'react-redux';
import { configureStore } from '@reduxjs/toolkit';
import { silApi } from '../../api/silApi';
import { Preflight } from '../Preflight';

function renderWithProviders() {
  const store = configureStore({
    reducer: { [silApi.reducerPath]: silApi.reducer },
    middleware: (gdm) => gdm().concat(silApi.middleware),
  });
  return render(
    <Provider store={store}>
      <Preflight />
    </Provider>
  );
}

describe('Preflight', () => {
  it('renders without crashing', () => {
    const { getByTestId } = renderWithProviders();
    expect(getByTestId('preflight')).toBeInTheDocument();
  });
});
