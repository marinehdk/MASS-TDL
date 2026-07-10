import { describe, expect, it, vi, beforeEach } from 'vitest';
import { fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { EvidenceLibraryView } from '../EvidenceLibraryView';

const apiMocks = vi.hoisted(() => ({
  rescan: vi.fn(),
  refetch: vi.fn(),
  deleteSession: vi.fn(),
  deleteUnwrap: vi.fn(),
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
          passed_scenarios: 1,
          scenario_ids: ['colreg-rule14-ho'],
          overview_png: { scenario_id: 'colreg-rule14-ho', relative_path: 'colreg-rule14-ho_trajectory_dashboard.png' },
          overview_pngs: [
            { scenario_id: 'colreg-rule14-ho', relative_path: 'colreg-rule14-ho_trajectory_dashboard.png' },
            { scenario_id: 'colreg-rule15-cs', relative_path: 'colreg-rule15-cs_trajectory_dashboard.png' },
          ],
          ingest_status: 'ok',
        },
        {
          evidence_id: 'fedcba98-7654-3210-fedc-ba9876543210',
          session_id: '20260708_091500_rule15_cs_clean8_full',
          source: 'frontend',
          suite: 'clean8',
          root_id: 'runs',
          worktree_name: 'evidence-library-replay-impl',
          branch: 'codex/evidence-library-replay-impl',
          session_path: '/tmp/runs/20260708_091500_rule15_cs_clean8_full',
          created_at: '2026-07-08T09:15:00Z',
          status: 'completed',
          valid_data: true,
          scenario_count: 1,
          failed_scenarios: 1,
          scenario_ids: ['colreg-rule15-cs'],
          overview_png: null,
          ingest_status: 'ok',
        },
      ],
    },
    isLoading: false,
    refetch: apiMocks.refetch,
  }),
  useRescanEvidenceLibraryMutation: () => [apiMocks.rescan, { isLoading: false }],
  useDeleteEvidenceLibrarySessionMutation: () => [apiMocks.deleteSession, { isLoading: false, error: null }],
}));

beforeEach(() => {
  apiMocks.rescan.mockReset();
  apiMocks.refetch.mockReset();
  apiMocks.deleteSession.mockReset();
  apiMocks.deleteUnwrap.mockReset();
  apiMocks.rescan.mockResolvedValue({ ingested: 1, pruned: 0, errors: [] });
  apiMocks.refetch.mockResolvedValue(undefined);
  apiMocks.deleteUnwrap.mockResolvedValue({
    evidence_id: '12345678-90ab-cdef-1234-567890abcdef',
    deleted_path: '/tmp/.worktrees/colregs-nlp-cpa-fix/runs/trace_eval/session',
    filesystem_deleted: true,
  });
  apiMocks.deleteSession.mockReturnValue({ unwrap: apiMocks.deleteUnwrap });
});

describe('EvidenceLibraryView', () => {
  it('lists indexed sessions and opens selected evidence session', () => {
    const onOpen = vi.fn();

    render(<EvidenceLibraryView onOpen={onOpen} />);

    expect(screen.getByText('仿真数据库')).toBeInTheDocument();
    expect(screen.getByText('12345678...')).toBeInTheDocument();
    expect(screen.getByText('2026-07-07 13:20:00')).toBeInTheDocument();
    expect(screen.queryByText('名称')).not.toBeInTheDocument();
    expect(screen.getByText('仿真结果')).toBeInTheDocument();
    expect(screen.getByText('通过')).toBeInTheDocument();
    expect(screen.getAllByText('单个场景').length).toBeGreaterThan(0);
    expect(screen.getAllByText('调试验证').length).toBeGreaterThan(0);
    expect(screen.getByText('仿真场景')).toBeInTheDocument();
    expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
    expect(screen.getAllByText('CLI').length).toBeGreaterThan(0);
    expect(screen.getByText('colregs-nlp-cpa-fix')).toBeInTheDocument();
    expect(screen.queryByText('能否回放')).not.toBeInTheDocument();
    const firstSessionRow = screen.getByText('12345678...').closest('tr');
    expect(firstSessionRow).not.toBeNull();
    fireEvent.click(within(firstSessionRow!).getByRole('button', { name: '概述' }));
    expect(screen.getByRole('dialog', { name: '仿真概述' })).toBeInTheDocument();
    expect(screen.getByText(/1\/2/)).toBeInTheDocument();
    expect(screen.getByAltText('colreg-rule14-ho 仿真概述')).toHaveAttribute(
      'src',
      '/api/v1/evidence-library/sessions/12345678-90ab-cdef-1234-567890abcdef/overview-png?scenario_id=colreg-rule14-ho',
    );
    fireEvent.click(screen.getByTitle('下一张'));
    expect(screen.getByAltText('colreg-rule15-cs 仿真概述')).toHaveAttribute(
      'src',
      '/api/v1/evidence-library/sessions/12345678-90ab-cdef-1234-567890abcdef/overview-png?scenario_id=colreg-rule15-cs',
    );
    fireEvent.click(screen.getByTitle('放大'));
    expect(screen.getByText('125%')).toBeInTheDocument();
    fireEvent.click(screen.getByTitle('重置'));
    expect(screen.getByText('100%')).toBeInTheDocument();
    fireEvent.click(screen.getByText('关闭'));
    fireEvent.click(within(firstSessionRow!).getByRole('button', { name: '回放' }));
    expect(onOpen).toHaveBeenCalledWith('12345678-90ab-cdef-1234-567890abcdef');
  });

  it('scans evidence library and refreshes indexed sessions', async () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    expect(apiMocks.rescan).toHaveBeenCalledWith({ force: false });
    await waitFor(() => expect(apiMocks.refetch).toHaveBeenCalled());
  });

  it('filters rows by scenario text', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.change(screen.getByRole('searchbox', { name: '筛选仿真记录' }), {
      target: { value: 'rule15' },
    });

    expect(screen.getByText('colreg-rule15-cs')).toBeInTheDocument();
    expect(screen.queryByText('colreg-rule14-ho')).not.toBeInTheDocument();
  });

  it('cancels deletion without sending a request', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '删除 20260707_132000_rule14_ho_fast_debug' }));

    expect(screen.getByRole('dialog', { name: '删除仿真记录' })).toBeInTheDocument();
    expect(screen.getByText('20260707_132000_rule14_ho_fast_debug')).toBeInTheDocument();
    expect(screen.getByText('/tmp/.worktrees/colregs-nlp-cpa-fix/runs/trace_eval/session')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '取消' }));

    expect(apiMocks.deleteSession).not.toHaveBeenCalled();
    expect(screen.queryByRole('dialog', { name: '删除仿真记录' })).not.toBeInTheDocument();
  });

  it('deletes a session, closes its overview, and refreshes the list', async () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    const firstSessionRow = screen.getByText('12345678...').closest('tr');
    expect(firstSessionRow).not.toBeNull();
    fireEvent.click(within(firstSessionRow!).getByRole('button', { name: '概述' }));
    expect(screen.getByRole('dialog', { name: '仿真概述' })).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '删除 20260707_132000_rule14_ho_fast_debug' }));
    fireEvent.click(screen.getByRole('button', { name: '确认删除' }));

    await waitFor(() => expect(apiMocks.deleteSession).toHaveBeenCalledWith('12345678-90ab-cdef-1234-567890abcdef'));
    await waitFor(() => expect(apiMocks.refetch).toHaveBeenCalled());
    expect(screen.queryByRole('dialog', { name: '删除仿真记录' })).not.toBeInTheDocument();
    expect(screen.queryByRole('dialog', { name: '仿真概述' })).not.toBeInTheDocument();
  });

  it('keeps the deletion dialog open and reports request failure', async () => {
    apiMocks.deleteUnwrap.mockRejectedValueOnce(new Error('delete failed'));
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '删除 20260707_132000_rule14_ho_fast_debug' }));
    fireEvent.click(screen.getByRole('button', { name: '确认删除' }));

    expect(await screen.findByRole('alert')).toHaveTextContent('删除失败');
    expect(screen.getByRole('dialog', { name: '删除仿真记录' })).toBeInTheDocument();
    expect(apiMocks.refetch).not.toHaveBeenCalled();
  });
});
