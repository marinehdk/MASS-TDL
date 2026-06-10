import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { beforeEach, describe, expect, it, vi } from 'vitest';

const mocks = vi.hoisted(() => ({
  inject: vi.fn(),
  clear: vi.fn(),
}));

vi.mock('../../../api/silApi', () => ({
  useInjectEncounterMutation: () => [mocks.inject],
  useClearEncountersMutation: () => [mocks.clear],
}));

import { EncounterInjectPanel } from '../EncounterInjectPanel';

describe('EncounterInjectPanel', () => {
  beforeEach(() => {
    mocks.inject.mockReset();
    mocks.clear.mockReset();
    mocks.inject.mockImplementation(() => ({
      unwrap: () => Promise.resolve({ accepted: true, mmsi: 990000001 + mocks.inject.mock.calls.length }),
    }));
    mocks.clear.mockImplementation(() => ({
      unwrap: () => Promise.resolve({ removed_count: 3 }),
    }));
  });

  it('injects three targets for the random demo button and clears via clear-all endpoint', async () => {
    render(<EncounterInjectPanel inline />);

    fireEvent.click(screen.getByTestId('encounter-random-demo'));
    await waitFor(() => expect(mocks.inject).toHaveBeenCalledTimes(3));

    fireEvent.click(screen.getByTestId('encounter-clear'));
    await waitFor(() => expect(mocks.clear).toHaveBeenCalledTimes(1));
  });

  it('keeps already injected targets visible when a later random demo injection fails', async () => {
    mocks.inject
      .mockImplementationOnce(() => ({
        unwrap: () => Promise.resolve({ accepted: true, mmsi: 990000101 }),
      }))
      .mockImplementationOnce(() => ({
        unwrap: () => Promise.reject({ data: { detail: 'sim not active' } }),
      }));

    render(<EncounterInjectPanel inline />);
    fireEvent.click(screen.getByTestId('encounter-random-demo'));

    await waitFor(() => expect(screen.getByText('清除注入船（1）')).toBeInTheDocument());
    expect(screen.getByText('sim not active')).toBeInTheDocument();
  });

  it('keeps failed clear-all MMSIs in the visible active count', async () => {
    mocks.clear.mockImplementation(() => ({
      unwrap: () => Promise.resolve({ removed_count: 2, failed_mmsis: [990000301] }),
    }));

    render(<EncounterInjectPanel inline />);
    fireEvent.click(screen.getByTestId('encounter-head_on'));
    await waitFor(() => expect(screen.getByText('清除注入船（1）')).toBeInTheDocument());

    fireEvent.click(screen.getByTestId('encounter-clear'));
    await waitFor(() => expect(screen.getByText('清除注入船（1）')).toBeInTheDocument());
  });
});
