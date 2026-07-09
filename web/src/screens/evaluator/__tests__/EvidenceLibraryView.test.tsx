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
          passed_scenarios: 1,
          scenario_ids: ['colreg-rule14-ho'],
          overview_png: { scenario_id: 'colreg-rule14-ho', relative_path: 'colreg-rule14-ho_trajectory_dashboard.png' },
          overview_pngs: [
            { scenario_id: 'colreg-rule14-ho', relative_path: 'colreg-rule14-ho_trajectory_dashboard.png' },
            { scenario_id: 'colreg-rule15-cs', relative_path: 'colreg-rule15-cs_trajectory_dashboard.png' },
          ],
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
    fireEvent.click(screen.getByText('概述'));
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
    fireEvent.click(screen.getByText('回放'));
    expect(onOpen).toHaveBeenCalledWith('12345678-90ab-cdef-1234-567890abcdef');
  });

  it('rescans evidence library and refreshes indexed sessions', async () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByText('刷新'));

    expect(apiMocks.rescan).toHaveBeenCalledWith({ force: false });
    await waitFor(() => expect(apiMocks.refetch).toHaveBeenCalled());
  });
});
