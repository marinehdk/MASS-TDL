import { describe, it, expect, vi } from 'vitest';
import { render, screen } from '@testing-library/react';
import { Provider } from 'react-redux';
import { configureStore } from '@reduxjs/toolkit';
import { silApi } from '../../api/silApi';
import { RunReport } from '../RunReport';

// Mock RTK Query to return real scoring data
vi.mock('../../api/silApi', async (importOriginal) => {
  const actual = await importOriginal() as any;
  return {
    ...actual,
    useGetLastRunScoringQuery: () => ({
      data: {
        run_id: 'test-run',
        verdict: 'pass',
        kpis: {
          min_cpa_nm: 0.247,
          avg_rot_dpm: 21.6,
          distance_nm: 0.99,
          duration_s: 700,
          max_rud_deg: 35.0,
          tor_time_s: null,
          decision_p99_ms: 312,
          faults_injected: 0,
          scenario_sha256: 'abc123',
        },
        rule_chain: ['Rule14_GW_HeadOn'],
      },
      refetch: vi.fn(),
    }),
    useExportMarzipMutation: () => [vi.fn(), { isLoading: false }],
    useGetExportStatusQuery: () => ({ data: null }),
  };
});

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
    renderWithProviders();
    expect(screen.getByText(/RUN REPORT/)).toBeTruthy();
  });

  it('shows export and navigation buttons', () => {
    renderWithProviders();
    expect(screen.getByText('← BACK TO BRIDGE')).toBeTruthy();
    expect(screen.getByText('↓ EXPORT ASDR')).toBeTruthy();
    expect(screen.getByText('NEW RUN →')).toBeTruthy();
  });

  it('shows min CPA from API, not hardcoded 0.52', () => {
    renderWithProviders();
    expect(screen.getByText('0.247 nm')).toBeTruthy();
  });

  it('shows verdict PASS from API', () => {
    renderWithProviders();
    const verdicts = screen.getAllByText('PASS');
    expect(verdicts.length).toBeGreaterThan(0);
  });
});
