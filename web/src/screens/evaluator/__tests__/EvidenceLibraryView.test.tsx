import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { EvidenceLibraryView } from '../EvidenceLibraryView';

const apiMocks = vi.hoisted(() => ({
  sessions: [] as Array<Record<string, unknown>>,
  rescan: vi.fn(),
  rescanUnwrap: vi.fn(),
  refetch: vi.fn(),
  deleteSession: vi.fn(),
  deleteUnwrap: vi.fn(),
}));

vi.mock('../../../api/silApi', () => ({
  useGetEvidenceLibrarySessionsQuery: () => ({
    data: { sessions: apiMocks.sessions },
    isLoading: false,
    refetch: apiMocks.refetch,
  }),
  useRescanEvidenceLibraryMutation: () => [apiMocks.rescan, { isLoading: false }],
  useDeleteEvidenceLibrarySessionMutation: () => [apiMocks.deleteSession, { isLoading: false, error: null }],
}));

const primarySession = {
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
};

const secondarySession = {
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
};

const makeSessions = (count: number) => Array.from({ length: count }, (_, index) => ({
  evidence_id: `page-${String(index).padStart(4, '0')}`,
  session_id: `page_session_${String(index).padStart(2, '0')}`,
  source: 'cli',
  suite: 'single',
  root_id: 'runs',
  worktree_name: 'pagination-worktree',
  branch: 'codex/pagination',
  session_path: `/tmp/runs/page_session_${String(index).padStart(2, '0')}`,
  created_at: new Date(Date.UTC(2026, 6, 1, 0, 0, index)).toISOString(),
  status: 'completed',
  valid_data: true,
  scenario_count: 1,
  passed_scenarios: 1,
  scenario_ids: [`page-scenario-${index}`],
  overview_png: null,
  ingest_status: 'ok',
}));

const deleteButton = (sessionId = primarySession.session_id) =>
  screen.getByRole('button', { name: `删除 ${sessionId}` });

beforeEach(() => {
  apiMocks.sessions = [{ ...primarySession }, { ...secondarySession }];
  apiMocks.rescan.mockReset();
  apiMocks.rescanUnwrap.mockReset();
  apiMocks.refetch.mockReset();
  apiMocks.deleteSession.mockReset();
  apiMocks.deleteUnwrap.mockReset();
  apiMocks.rescanUnwrap.mockResolvedValue({ ingested: 1, pruned: 0, errors: [] });
  apiMocks.rescan.mockReturnValue({ unwrap: apiMocks.rescanUnwrap });
  apiMocks.refetch.mockResolvedValue(undefined);
  apiMocks.deleteUnwrap.mockResolvedValue({
    evidence_id: primarySession.evidence_id,
    deleted_path: primarySession.session_path,
    filesystem_deleted: true,
  });
  apiMocks.deleteSession.mockReturnValue({ unwrap: apiMocks.deleteUnwrap });
});

describe('EvidenceLibraryView', () => {
  it('lists indexed sessions and preserves overview navigation, zoom, pan, reset, and replay', () => {
    const onOpen = vi.fn();
    render(<EvidenceLibraryView onOpen={onOpen} />);

    expect(screen.getByText('仿真数据库')).toBeInTheDocument();
    expect(screen.getByText('12345678...')).toBeInTheDocument();
    expect(screen.getByText('2026-07-07 13:20:00')).toBeInTheDocument();
    expect(screen.getByText('仿真结果')).toBeInTheDocument();
    expect(screen.getByText('通过')).toBeInTheDocument();
    expect(screen.getAllByText('单个场景').length).toBeGreaterThan(0);
    expect(screen.getAllByText('调试验证').length).toBeGreaterThan(0);
    expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
    expect(screen.getAllByText('CLI').length).toBeGreaterThan(0);
    expect(screen.getByText('colregs-nlp-cpa-fix')).toBeInTheDocument();

    const firstSessionRow = screen.getByText('12345678...').closest('tr');
    expect(firstSessionRow).not.toBeNull();
    fireEvent.click(within(firstSessionRow!).getByRole('button', { name: '概述' }));
    expect(screen.getByRole('dialog', { name: '仿真概述' })).toBeInTheDocument();
    expect(screen.getByText(/1\/2/)).toBeInTheDocument();

    fireEvent.click(screen.getByTitle('下一张'));
    const overviewImage = screen.getByAltText('colreg-rule15-cs 仿真概述');
    expect(overviewImage).toHaveAttribute(
      'src',
      '/api/v1/evidence-library/sessions/12345678-90ab-cdef-1234-567890abcdef/overview-png?scenario_id=colreg-rule15-cs',
    );
    fireEvent.click(screen.getByTitle('放大'));
    expect(screen.getByText('125%')).toBeInTheDocument();

    const panSurface = overviewImage.parentElement;
    expect(panSurface).not.toBeNull();
    fireEvent.mouseDown(panSurface!, { clientX: 10, clientY: 20 });
    fireEvent.mouseMove(panSurface!, { clientX: 35, clientY: 55 });
    fireEvent.mouseUp(panSurface!);
    expect(overviewImage).toHaveStyle({ transform: 'translate(25px, 35px) scale(1.25)' });

    fireEvent.click(screen.getByTitle('重置'));
    expect(screen.getByText('100%')).toBeInTheDocument();
    expect(overviewImage).toHaveStyle({ transform: 'translate(0px, 0px) scale(1)' });
    fireEvent.click(screen.getByRole('button', { name: '关闭' }));
    fireEvent.click(within(firstSessionRow!).getByRole('button', { name: '回放' }));
    expect(onOpen).toHaveBeenCalledWith(primarySession.evidence_id);
  });

  it('defaults automatic scans to 24 hours', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getByRole('combobox', { name: '自动刷新间隔' })).toHaveValue('86400');
    expect(screen.getByLabelText('距离下次扫描')).toHaveTextContent('24:00:00');
  });

  it('unwraps a successful scan before refreshing indexed sessions', async () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    expect(apiMocks.rescan).toHaveBeenCalledWith({ force: false });
    await waitFor(() => expect(apiMocks.rescanUnwrap).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(apiMocks.refetch).toHaveBeenCalledTimes(1));
  });

  it('keeps the countdown and rows unchanged and alerts when scan unwrap rejects', async () => {
    vi.useFakeTimers();
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    apiMocks.rescanUnwrap.mockRejectedValueOnce(new Error('scan failed'));

    try {
      act(() => vi.advanceTimersByTime(1000));
      expect(screen.getByLabelText('距离下次扫描')).toHaveTextContent('23:59:59');

      await act(async () => {
        fireEvent.click(screen.getByRole('button', { name: '扫描' }));
        await Promise.resolve();
      });

      expect(apiMocks.rescanUnwrap).toHaveBeenCalledTimes(1);
      expect(apiMocks.refetch).not.toHaveBeenCalled();
      expect(screen.getByRole('alert')).toHaveTextContent('扫描失败');
      expect(screen.getByLabelText('距离下次扫描')).toHaveTextContent('23:59:59');
      expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
    } finally {
      view.unmount();
      vi.useRealTimers();
    }
  });

  it('does not reset an elapsed automatic countdown after scan rejection', async () => {
    vi.useFakeTimers();
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    apiMocks.rescanUnwrap.mockRejectedValueOnce(new Error('scan failed'));

    try {
      fireEvent.change(screen.getByRole('combobox', { name: '自动刷新间隔' }), {
        target: { value: '600' },
      });
      for (let second = 0; second < 599; second += 1) {
        act(() => vi.advanceTimersByTime(1_000));
      }
      expect(screen.getByLabelText('距离下次扫描')).toHaveTextContent('0:01');
      await act(async () => {
        vi.advanceTimersByTime(1_000);
        await Promise.resolve();
      });

      expect(apiMocks.rescanUnwrap).toHaveBeenCalledTimes(1);
      expect(apiMocks.refetch).not.toHaveBeenCalled();
      expect(screen.getByRole('alert')).toHaveTextContent('扫描失败');
      expect(screen.getByLabelText('距离下次扫描')).toHaveTextContent(/^0:00$/);
    } finally {
      view.unmount();
      vi.useRealTimers();
    }
  });

  it.each([
    ['ID', 'fedcba98'],
    ['scenario', 'rule15'],
    ['source', 'frontend'],
    ['worktree', 'evidence-library-replay-impl'],
    ['suite', 'clean8'],
    ['mode', '完整验证'],
    ['result', '不通过'],
  ])('filters rows by %s search text', (_dimension, query) => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.change(screen.getByRole('searchbox', { name: '筛选仿真记录' }), {
      target: { value: query },
    });

    expect(deleteButton(secondarySession.session_id)).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: `删除 ${primarySession.session_id}` })).not.toBeInTheDocument();
  });

  it('composes enum and text filters', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const search = screen.getByRole('searchbox', { name: '筛选仿真记录' });
    const sourceFilter = screen.getByRole('combobox', { name: 'source filter' });

    fireEvent.change(search, { target: { value: 'rule15' } });
    fireEvent.change(sourceFilter, { target: { value: 'CLI' } });
    expect(screen.queryByRole('button', { name: `删除 ${secondarySession.session_id}` })).not.toBeInTheDocument();

    fireEvent.change(sourceFilter, { target: { value: 'Front' } });
    expect(deleteButton(secondarySession.session_id)).toBeInTheDocument();
  });

  it('shows a visible focus treatment on the search field', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const search = screen.getByRole('searchbox', { name: '筛选仿真记录' });

    fireEvent.focus(search);

    expect(search).toHaveStyle({ outline: '2px solid var(--c-phos)' });
  });

  it('supports 20 and 50 row pages', () => {
    apiMocks.sessions = makeSessions(51);
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getAllByRole('button', { name: /^删除 page_session_/ })).toHaveLength(20);
    fireEvent.change(screen.getByRole('combobox', { name: '每页记录数' }), { target: { value: '50' } });
    expect(screen.getAllByRole('button', { name: /^删除 page_session_/ })).toHaveLength(50);
    fireEvent.click(screen.getByRole('button', { name: '下一页' }));
    expect(screen.getAllByRole('button', { name: /^删除 page_session_/ })).toHaveLength(1);
  });

  it('resets to the first page when search changes', () => {
    apiMocks.sessions = makeSessions(25);
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '下一页' }));
    expect(screen.getByText('2 / 2')).toBeInTheDocument();
    fireEvent.change(screen.getByRole('searchbox', { name: '筛选仿真记录' }), {
      target: { value: 'page_session_00' },
    });

    expect(screen.getByText('1 / 1')).toBeInTheDocument();
    expect(deleteButton('page_session_00')).toBeInTheDocument();
  });

  it('moves focus into deletion dialog, traps Tab, makes background inert, and restores focus on cancel', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const trigger = deleteButton();
    trigger.focus();

    fireEvent.click(trigger);

    const dialog = screen.getByRole('dialog', { name: '删除仿真记录' });
    const cancel = screen.getByRole('button', { name: '取消' });
    const confirm = screen.getByRole('button', { name: '确认删除' });
    expect(cancel).toHaveFocus();
    expect(document.querySelector('main')).toHaveAttribute('inert');
    expect(document.querySelector('main')).toHaveAttribute('aria-hidden', 'true');

    fireEvent.keyDown(dialog, { key: 'Tab', shiftKey: true });
    expect(confirm).toHaveFocus();
    fireEvent.keyDown(dialog, { key: 'Tab' });
    expect(cancel).toHaveFocus();

    fireEvent.click(cancel);
    expect(screen.queryByRole('dialog', { name: '删除仿真记录' })).not.toBeInTheDocument();
    expect(trigger).toHaveFocus();
    expect(apiMocks.deleteSession).not.toHaveBeenCalled();
  });

  it('closes deletion dialog on Escape and restores triggering focus', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const trigger = deleteButton();
    fireEvent.click(trigger);

    fireEvent.keyDown(screen.getByRole('dialog', { name: '删除仿真记录' }), { key: 'Escape' });

    expect(screen.queryByRole('dialog', { name: '删除仿真记录' })).not.toBeInTheDocument();
    expect(trigger).toHaveFocus();
  });

  it('deletes a session, restores triggering focus, and refreshes the list', async () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const trigger = deleteButton();
    fireEvent.click(trigger);
    fireEvent.click(screen.getByRole('button', { name: '确认删除' }));

    await waitFor(() => expect(apiMocks.deleteSession).toHaveBeenCalledWith(primarySession.evidence_id));
    await waitFor(() => expect(apiMocks.refetch).toHaveBeenCalledTimes(1));
    expect(screen.queryByRole('dialog', { name: '删除仿真记录' })).not.toBeInTheDocument();
    expect(trigger).toHaveFocus();
  });

  it('keeps the deletion dialog open and reports request failure', async () => {
    apiMocks.deleteUnwrap.mockRejectedValueOnce(new Error('delete failed'));
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(deleteButton());
    fireEvent.click(screen.getByRole('button', { name: '确认删除' }));

    expect(await screen.findByRole('alert')).toHaveTextContent('删除失败');
    expect(screen.getByRole('dialog', { name: '删除仿真记录' })).toBeInTheDocument();
    expect(apiMocks.refetch).not.toHaveBeenCalled();
  });
});
