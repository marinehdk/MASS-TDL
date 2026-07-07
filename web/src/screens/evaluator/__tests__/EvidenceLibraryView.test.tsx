import { describe, expect, it, vi, beforeEach } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/react';
import { EvidenceLibraryView } from '../EvidenceLibraryView';

const apiMocks = vi.hoisted(() => ({
  rescan: vi.fn(),
  refetch: vi.fn(),
}));

vi.mock('../../../api/silApi', () => ({
  useGetEvidenceLibrarySessionsQuery: () => ({
    data: {
      sessions: [
        {
          evidence_id: 'ev-1',
          session_id: '20260707_132000_single_colreg-rule14-ho',
          source: 'cli',
          suite: 'single',
          root_id: 'worktrees',
          worktree_name: 'colregs-nlp-cpa-fix',
          branch: 'codex/colregs-nlp-cpa-fix',
          session_path: '/tmp/runs/trace_eval/session',
          created_at: '2026-07-07T13:20:00Z',
          status: 'completed',
          valid_data: true,
          scenario_count: 1,
          scenario_ids: ['colreg-rule14-ho'],
          ingest_status: 'ok',
        },
      ],
    },
    isLoading: false,
    refetch: apiMocks.refetch,
  }),
  useRescanEvidenceLibraryMutation: () => [apiMocks.rescan, { isLoading: false }],
}));

beforeEach(() => {
  apiMocks.rescan.mockResolvedValue({ ingested: 1, errors: [] });
  apiMocks.refetch.mockResolvedValue(undefined);
});

describe('EvidenceLibraryView', () => {
  it('lists indexed sessions and opens selected evidence session', () => {
    const onOpen = vi.fn();

    render(<EvidenceLibraryView onOpen={onOpen} />);

    expect(screen.getByText('Evidence Library')).toBeInTheDocument();
    expect(screen.getByText('20260707_132000_single_colreg-rule14-ho')).toBeInTheDocument();
    expect(screen.getByText('cli')).toBeInTheDocument();
    expect(screen.getByText('worktrees')).toBeInTheDocument();
    expect(screen.getByText('colregs-nlp-cpa-fix')).toBeInTheDocument();
    expect(screen.getByText('ok')).toBeInTheDocument();
    fireEvent.click(screen.getByText('Open Replay'));
    expect(onOpen).toHaveBeenCalledWith('ev-1');
  });

  it('rescans evidence library and refreshes indexed sessions', async () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByText('Rescan'));

    expect(apiMocks.rescan).toHaveBeenCalledWith({ force: false });
    await waitFor(() => expect(apiMocks.refetch).toHaveBeenCalled());
  });
});
