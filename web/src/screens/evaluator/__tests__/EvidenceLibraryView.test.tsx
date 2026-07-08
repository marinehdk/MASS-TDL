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
          evidence_id: '12345678-90ab-cdef-1234-567890abcdef',
          session_id: '20260707_132000_rule14_ho_fast_debug',
          source: 'cli',
          suite: 'single',
          root_id: 'worktrees',
          worktree_name: null,
          branch: 'codex/colregs-nlp-cpa-fix',
          session_path: '/tmp/.worktrees/colregs-nlp-cpa-fix/runs/trace_eval/session',
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

    expect(screen.getByText('仿真数据库')).toBeInTheDocument();
    expect(screen.getByText('12345678...')).toBeInTheDocument();
    expect(screen.getByText('2026-07-07 13:20:00')).toBeInTheDocument();
    expect(screen.getByText('rule14_ho_fast_debug')).toBeInTheDocument();
    expect(screen.getAllByText('单个场景').length).toBeGreaterThan(0);
    expect(screen.getAllByText('调试验证').length).toBeGreaterThan(0);
    expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
    expect(screen.getAllByText('CLI').length).toBeGreaterThan(0);
    expect(screen.getByText('colregs-nlp-cpa-fix')).toBeInTheDocument();
    expect(screen.getAllByText('OK').length).toBeGreaterThan(0);
    fireEvent.click(screen.getByText('Open'));
    expect(onOpen).toHaveBeenCalledWith('12345678-90ab-cdef-1234-567890abcdef');
  });

  it('rescans evidence library and refreshes indexed sessions', async () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByText('刷新'));

    expect(apiMocks.rescan).toHaveBeenCalledWith({ force: false });
    await waitFor(() => expect(apiMocks.refetch).toHaveBeenCalled());
  });
});
