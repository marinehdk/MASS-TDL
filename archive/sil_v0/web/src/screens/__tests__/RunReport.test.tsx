import { describe, it, expect } from 'vitest';
import { render } from '@testing-library/react';
import { Provider } from 'react-redux';
import { configureStore } from '@reduxjs/toolkit';
import { silApi } from '../../api/silApi';
import { RunReport } from '../RunReport';

function renderWithProviders() {
  const store = configureStore({
    reducer: { [silApi.reducerPath]: silApi.reducer },
    middleware: (gdm) => gdm().concat(silApi.middleware),
  });
  return render(
    <Provider store={store}>
      <RunReport />
    </Provider>
  );
}

describe('RunReport', () => {
  it('renders without crashing', () => {
    const { getByTestId } = renderWithProviders();
    expect(getByTestId('run-report')).toBeInTheDocument();
  });

  it('shows verdict and export buttons', () => {
    const { getByTestId } = renderWithProviders();
    expect(getByTestId('btn-pass')).toBeInTheDocument();
    expect(getByTestId('btn-fail')).toBeInTheDocument();
    expect(getByTestId('btn-export')).toBeInTheDocument();
  });
});
