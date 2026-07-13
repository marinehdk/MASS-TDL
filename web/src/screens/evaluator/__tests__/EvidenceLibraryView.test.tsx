import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { configureStore } from '@reduxjs/toolkit';
import { Provider } from 'react-redux';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { EvidenceReplaySession } from '../../../api/silApi';
import { EvidenceLibraryView } from '../EvidenceLibraryView';

const apiMocks = vi.hoisted(() => ({
  sessions: [] as Array<Record<string, unknown>>,
  rescan: vi.fn(),
  rescanUnwrap: vi.fn(),
  refetch: vi.fn(),
  deleteSession: vi.fn(),
  deleteUnwrap: vi.fn(),
  deleteIsLoading: false,
}));

vi.mock('../../../api/silApi', () => ({
  useGetEvidenceLibrarySessionsQuery: () => ({
    data: { sessions: apiMocks.sessions },
    isLoading: false,
    refetch: apiMocks.refetch,
  }),
  useRescanEvidenceLibraryMutation: () => [apiMocks.rescan, { isLoading: false }],
  useDeleteEvidenceLibrarySessionMutation: () => [
    apiMocks.deleteSession,
    { isLoading: apiMocks.deleteIsLoading, error: null },
  ],
}));

const primarySession = {
  evidence_id: '12345678-90ab-cdef-1234-567890abcdef',
  session_id: '20260707_132000_rule14_ho_fast_debug',
  source: 'cli',
  suite: 'single',
  root_id: 'worktrees',
  worktree_name: null,
  branch: 'codex/colregs-nlp-cpa-fix',
  session_path: '/tmp/.worktrees/colregs-nlp-cpa-fix/runs/20260707_132000_rule14_ho_fast_debug/trace',
  deletion_allowed: true,
  deletion_target: '/tmp/.worktrees/colregs-nlp-cpa-fix/runs/20260707_132000_rule14_ho_fast_debug',
  deletion_error: null,
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
  deletion_allowed: true,
  deletion_target: '/tmp/runs/20260708_091500_rule15_cs_clean8_full',
  deletion_error: null,
  created_at: '2026-07-08T09:15:00Z',
  status: 'completed',
  valid_data: true,
  scenario_count: 1,
  failed_scenarios: 1,
  scenario_ids: ['colreg-rule15-cs'],
  overview_png: null,
  ingest_status: 'ok',
};

const {
  deletion_allowed: _deletionAllowed,
  deletion_target: _deletionTarget,
  deletion_error: _deletionError,
  scenario_ids: _scenarioIds,
  ...replaySessionWireFields
} = primarySession;
const replaySessionWireFixture: EvidenceReplaySession = replaySessionWireFields;

const makeSessions = (count: number) => Array.from({ length: count }, (_, index) => ({
  evidence_id: `page-${String(index).padStart(4, '0')}`,
  session_id: `page_session_${String(index).padStart(2, '0')}`,
  source: 'cli',
  suite: 'single',
  root_id: 'runs',
  worktree_name: 'pagination-worktree',
  branch: 'codex/pagination',
  session_path: `/tmp/runs/page_session_${String(index).padStart(2, '0')}`,
  deletion_allowed: true,
  deletion_target: `/tmp/runs/page_session_${String(index).padStart(2, '0')}`,
  deletion_error: null,
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
  apiMocks.deleteIsLoading = false;
  apiMocks.rescanUnwrap.mockResolvedValue({ ingested: 1, pruned: 0, errors: [] });
  apiMocks.rescan.mockReturnValue({ unwrap: apiMocks.rescanUnwrap });
  apiMocks.refetch.mockResolvedValue(undefined);
  apiMocks.deleteUnwrap.mockResolvedValue({
    evidence_id: primarySession.evidence_id,
    deleted_path: primarySession.deletion_target,
    filesystem_deleted: true,
  });
  apiMocks.deleteSession.mockReturnValue({ unwrap: apiMocks.deleteUnwrap });
});

describe('EvidenceLibraryView', () => {
  it('lists indexed sessions and preserves overview navigation, zoom, pan, reset, and replay', () => {
    const onOpen = vi.fn();
    render(<EvidenceLibraryView onOpen={onOpen} />);

    expect(screen.getByText('仿真数据库')).toBeInTheDocument();
    expect(screen.getByRole('columnheader', { name: /序号/ })).toBeInTheDocument();
    expect(screen.getByText('2026-07-07 13:20:00')).toBeInTheDocument();
    expect(screen.getByText('仿真结果')).toBeInTheDocument();
    expect(screen.getByText('通过')).toBeInTheDocument();
    expect(screen.getByRole('columnheader', { name: /场景数量/ })).toBeInTheDocument();
    expect(screen.getAllByText('调试验证').length).toBeGreaterThan(0);
    expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
    expect(screen.getAllByText('CLI').length).toBeGreaterThan(0);
    expect(screen.getByText('colregs-nlp-cpa-fix')).toBeInTheDocument();

    const firstSessionRow = screen.getByText('colreg-rule14-ho').closest('tr');
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

  it('unwraps a successful scan without manually duplicating RTK invalidation', async () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    expect(apiMocks.rescan).toHaveBeenCalledWith({ force: false });
    await waitFor(() => expect(apiMocks.rescanUnwrap).toHaveBeenCalledTimes(1));
    expect(apiMocks.refetch).not.toHaveBeenCalled();
  });

  it('shows every HTTP-200 scan error while retaining successful result counts', async () => {
    apiMocks.rescanUnwrap.mockResolvedValueOnce({
      ingested: 2,
      pruned: 1,
      errors: [
        { path: '/runs/broken-a', error: 'manifest missing' },
        { path: '/runs/broken-b', error: 'trajectory unreadable' },
      ],
    });
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    const alert = await screen.findByRole('alert');
    expect(alert).toHaveTextContent('扫描部分完成');
    expect(alert).toHaveTextContent('写入 2');
    expect(alert).toHaveTextContent('清理 1');
    expect(alert).toHaveTextContent('/runs/broken-a');
    expect(alert).toHaveTextContent('manifest missing');
    expect(alert).toHaveTextContent('/runs/broken-b');
    expect(alert).toHaveTextContent('trajectory unreadable');
    expect(alert).not.toHaveTextContent('扫描成功');
    expect(apiMocks.refetch).not.toHaveBeenCalled();
  });

  it('always shows the latest successful scan counts across a later request failure', async () => {
    apiMocks.rescanUnwrap
      .mockResolvedValueOnce({ ingested: 3, pruned: 2, errors: [] })
      .mockRejectedValueOnce(new Error('later scan failed'));
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));
    expect(await screen.findByRole('status')).toHaveTextContent('写入 3');
    expect(screen.getByRole('status')).toHaveTextContent('清理 2');
    expect(screen.getByRole('status')).toHaveTextContent('错误 0');

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('扫描失败');
    expect(screen.getByRole('status')).toHaveTextContent('写入 3');
    expect(screen.getByRole('status')).toHaveTextContent('清理 2');
    expect(screen.getByRole('status')).toHaveTextContent('错误 0');
  });

  it('waits for the selected interval after an automatic scan returns payload errors', async () => {
    vi.useFakeTimers();
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    apiMocks.rescanUnwrap.mockResolvedValueOnce({
      ingested: 1,
      pruned: 0,
      errors: [{ path: '/runs/broken', error: 'index failed' }],
    });

    try {
      fireEvent.change(screen.getByRole('combobox', { name: '自动刷新间隔' }), {
        target: { value: '600' },
      });
      await act(async () => {
        vi.advanceTimersByTime(600_000);
        await Promise.resolve();
      });
      expect(apiMocks.rescanUnwrap).toHaveBeenCalledTimes(1);
      expect(screen.getByRole('alert')).toHaveTextContent('扫描部分完成');

      await act(async () => {
        vi.advanceTimersByTime(5_000);
        await Promise.resolve();
      });
      expect(apiMocks.rescanUnwrap).toHaveBeenCalledTimes(1);
      expect(screen.getByLabelText('距离下次扫描')).toHaveTextContent('9:55');
    } finally {
      view.unmount();
      vi.useRealTimers();
    }
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

  it('opens compact filter menus for scenario counts and applies a selection', () => {
    apiMocks.sessions = [
      { ...primarySession, scenario_count: 1, scenario_ids: ['one'] },
      { ...secondarySession, scenario_count: 2, scenario_ids: ['two-a', 'two-b'] },
    ];
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    const trigger = screen.getByRole('button', { name: '筛选场景数量' });
    fireEvent.click(trigger);

    expect(trigger).toHaveAttribute('aria-expanded', 'true');
    const menu = screen.getByRole('menu', { name: '场景数量筛选选项' });
    expect(within(menu).getAllByRole('menuitem').map((item) => item.textContent)).toEqual(['全部', '1', '2']);

    fireEvent.click(within(menu).getByRole('menuitem', { name: '2' }));

    expect(screen.getByLabelText('场景数量筛选值')).toHaveTextContent('2');
    expect(trigger).toHaveAttribute('aria-expanded', 'false');
    expect(trigger).toHaveFocus();
    expect(screen.getAllByRole('row')).toHaveLength(2);
  });

  it('filters by mode and source with compact menus', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '筛选模式' }));
    fireEvent.click(screen.getByRole('menuitem', { name: '完整验证' }));
    expect(screen.getByLabelText('模式筛选值')).toHaveTextContent('完整验证');

    fireEvent.click(screen.getByRole('button', { name: '筛选来源' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Front' }));
    expect(screen.getByLabelText('来源筛选值')).toHaveTextContent('Front');
    expect(deleteButton(secondarySession.session_id)).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: `删除 ${primarySession.session_id}` })).not.toBeInTheDocument();
  });

  it('keeps only one compact filter menu open', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '筛选模式' }));
    expect(screen.getByRole('menu', { name: '模式筛选选项' })).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: '筛选来源' }));
    expect(screen.queryByRole('menu', { name: '模式筛选选项' })).not.toBeInTheDocument();
    expect(screen.getByRole('menu', { name: '来源筛选选项' })).toBeInTheDocument();
  });

  it('closes filter menus on outside pointerdown and Escape', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const trigger = screen.getByRole('button', { name: '筛选来源' });

    fireEvent.click(trigger);
    fireEvent.pointerDown(document.body);
    expect(screen.queryByRole('menu', { name: '来源筛选选项' })).not.toBeInTheDocument();

    fireEvent.click(trigger);
    fireEvent.keyDown(document, { key: 'Escape' });
    expect(screen.queryByRole('menu', { name: '来源筛选选项' })).not.toBeInTheDocument();
    expect(trigger).toHaveFocus();
  });

  it('resets pages after popover selection', () => {
    apiMocks.sessions = makeSessions(21);
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '下一页' }));
    expect(screen.getByText('2 / 2')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '筛选来源' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'CLI' }));

    expect(screen.getByText('1 / 2')).toBeInTheDocument();
  });

  it('composes popover filters with search', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const search = screen.getByRole('searchbox', { name: '筛选仿真记录' });

    fireEvent.change(search, { target: { value: 'rule15' } });
    fireEvent.click(screen.getByRole('button', { name: '筛选来源' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'CLI' }));
    expect(screen.queryByRole('button', { name: `删除 ${secondarySession.session_id}` })).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: '筛选来源' }));
    fireEvent.click(screen.getByRole('menuitem', { name: 'Front' }));
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

  it('renders continuous row numbers', () => {
    apiMocks.sessions = makeSessions(21);
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getByRole('columnheader', { name: /序号/ })).toBeInTheDocument();
    expect(screen.queryByText('会话ID')).not.toBeInTheDocument();
    expect(screen.getByRole('columnheader', { name: /场景数量/ })).toBeInTheDocument();
    expect(screen.queryByText('套件')).not.toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: '下一页' }));

    const firstDataRow = screen.getAllByRole('row')[1];
    expect(within(firstDataRow).getByText('21')).toBeInTheDocument();
  });

  it('formats run time to whole seconds', () => {
    apiMocks.sessions = [{ ...primarySession, created_at: '2026-07-07T13:20:00.123Z' }];
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getByText('2026-07-07 13:20:00')).toBeInTheDocument();
    expect(screen.queryByText(/13:20:00\.\d+/)).not.toBeInTheDocument();
  });

  it('uses separate sort directions', () => {
    apiMocks.sessions = [
      { ...primarySession, created_at: '2026-07-07T13:20:00Z' },
      { ...secondarySession, created_at: '2026-07-08T09:15:00Z' },
    ];
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    const ascending = screen.getByRole('button', { name: '按仿真时间升序' });
    const descending = screen.getByRole('button', { name: '按仿真时间降序' });

    fireEvent.click(ascending);
    expect(ascending).toHaveAttribute('aria-pressed', 'true');
    expect(descending).toHaveAttribute('aria-pressed', 'false');
    expect(screen.getAllByRole('row')[1]).toHaveTextContent('2026-07-07 13:20:00');

    fireEvent.click(descending);
    expect(ascending).toHaveAttribute('aria-pressed', 'false');
    expect(descending).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getAllByRole('row')[1]).toHaveTextContent('2026-07-08 09:15:00');
  });

  it('sorts scenario counts in both directions while preserving its compact filter', () => {
    apiMocks.sessions = [
      {
        ...primarySession,
        evidence_id: 'count-two',
        session_id: 'count_two',
        scenario_count: 2,
        scenario_ids: ['two-a', 'two-b'],
      },
      {
        ...primarySession,
        evidence_id: 'count-ten',
        session_id: 'count_ten',
        scenario_count: 10,
        scenario_ids: Array.from({ length: 10 }, (_, index) => `ten-${index + 1}`),
      },
      {
        ...primarySession,
        evidence_id: 'count-one',
        session_id: 'count_one',
        scenario_count: 1,
        scenario_ids: ['one'],
      },
    ];
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    const ascending = screen.getByRole('button', { name: '按场景数量升序' });
    const descending = screen.getByRole('button', { name: '按场景数量降序' });

    fireEvent.click(ascending);
    expect(ascending).toHaveAttribute('aria-pressed', 'true');
    expect(descending).toHaveAttribute('aria-pressed', 'false');
    expect(screen.getAllByRole('row')[1]).toHaveTextContent('one');

    fireEvent.click(descending);
    expect(ascending).toHaveAttribute('aria-pressed', 'false');
    expect(descending).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getAllByRole('row')[1]).toHaveTextContent('ten-1 +9');

    fireEvent.click(screen.getByRole('button', { name: '筛选场景数量' }));
    fireEvent.click(screen.getByRole('menuitem', { name: '2' }));
    expect(screen.getAllByRole('row')).toHaveLength(2);
    expect(screen.getAllByRole('row')[1]).toHaveTextContent('two-a, two-b');
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

  it('shows only the exact server-derived deletion target', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(deleteButton());

    const dialog = screen.getByRole('dialog', { name: '删除仿真记录' });
    expect(dialog).toHaveTextContent(primarySession.deletion_target);
    expect(dialog).not.toHaveTextContent(primarySession.session_path);
  });

  it('disables deletion when the backend marks the target unsafe', () => {
    apiMocks.sessions = [{
      ...primarySession,
      deletion_allowed: false,
      deletion_target: null,
      deletion_error: 'Evidence root must be enabled and trusted for deletion',
    }];
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    const trigger = deleteButton();
    expect(trigger).toBeDisabled();
    fireEvent.click(trigger);
    expect(screen.queryByRole('dialog', { name: '删除仿真记录' })).not.toBeInTheDocument();
  });

  it('closes deletion dialog on Escape and restores triggering focus', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const trigger = deleteButton();
    fireEvent.click(trigger);

    fireEvent.keyDown(screen.getByRole('dialog', { name: '删除仿真记录' }), { key: 'Escape' });

    expect(screen.queryByRole('dialog', { name: '删除仿真记录' })).not.toBeInTheDocument();
    expect(trigger).toHaveFocus();
  });

  it('blocks Escape, cancel, and overlay close while deletion is in flight', async () => {
    let rejectDelete!: (reason?: unknown) => void;
    const pendingDelete = new Promise<never>((_resolve, reject) => {
      rejectDelete = reject;
    });
    apiMocks.deleteUnwrap.mockReturnValueOnce(pendingDelete);
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(deleteButton());
    fireEvent.click(screen.getByRole('button', { name: '确认删除' }));
    apiMocks.deleteIsLoading = true;
    view.rerender(<EvidenceLibraryView onOpen={vi.fn()} />);

    const dialog = screen.getByRole('dialog', { name: '删除仿真记录' });
    expect(screen.getByRole('button', { name: '取消' })).toBeDisabled();
    fireEvent.keyDown(dialog, { key: 'Escape' });
    expect(screen.getByRole('dialog', { name: '删除仿真记录' })).toBeInTheDocument();
    fireEvent.click(dialog);
    expect(screen.getByRole('dialog', { name: '删除仿真记录' })).toBeInTheDocument();

    await act(async () => rejectDelete(new Error('cleanup')));
  });

  it('moves focus to stable search after refreshed data removes the deleted trigger', async () => {
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const trigger = deleteButton();
    fireEvent.click(trigger);
    fireEvent.click(screen.getByRole('button', { name: '确认删除' }));

    await waitFor(() => expect(apiMocks.deleteSession).toHaveBeenCalledWith(primarySession.evidence_id));
    await waitFor(() => expect(screen.queryByRole('dialog', { name: '删除仿真记录' })).not.toBeInTheDocument());

    apiMocks.sessions = [{ ...secondarySession }];
    view.rerender(<EvidenceLibraryView onOpen={vi.fn()} />);

    await waitFor(() => expect(screen.getByRole('searchbox', { name: '筛选仿真记录' })).toHaveFocus());
    expect(screen.queryByRole('button', { name: `删除 ${primarySession.session_id}` })).not.toBeInTheDocument();
    expect(apiMocks.refetch).not.toHaveBeenCalled();
  });

  it('restores stable focus and keeps remaining delete actions enabled without waiting for row reconciliation', async () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(deleteButton(primarySession.session_id));
    fireEvent.click(screen.getByRole('button', { name: '确认删除' }));

    await waitFor(() => expect(apiMocks.deleteSession).toHaveBeenCalledWith(primarySession.evidence_id));
    await waitFor(() => expect(screen.queryByRole('dialog', { name: '删除仿真记录' })).not.toBeInTheDocument());
    await waitFor(() => expect(screen.getByRole('searchbox', { name: '筛选仿真记录' })).toHaveFocus());
    expect(deleteButton(secondarySession.session_id)).toBeEnabled();
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

  it('keeps the previous delete error visible while a retry is loading', async () => {
    let rejectRetry!: (reason?: unknown) => void;
    const pendingRetry = new Promise<never>((_resolve, reject) => {
      rejectRetry = reject;
    });
    apiMocks.deleteUnwrap
      .mockRejectedValueOnce(new Error('first delete failed'))
      .mockReturnValueOnce(pendingRetry);
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(deleteButton());
    fireEvent.click(screen.getByRole('button', { name: '确认删除' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('删除失败');

    fireEvent.click(screen.getByRole('button', { name: '确认删除' }));
    apiMocks.deleteIsLoading = true;
    view.rerender(<EvidenceLibraryView onOpen={vi.fn()} />);
    expect(screen.getByRole('alert')).toHaveTextContent('删除失败');

    await act(async () => rejectRetry(new Error('retry failed')));
  });
});

describe('evidence library RTK invalidation', () => {
  it('accepts replay session wire payloads without list-only deletion preview fields', () => {
    expect(replaySessionWireFixture.evidence_id).toBe(primarySession.evidence_id);
    expect(replaySessionWireFixture).not.toHaveProperty('deletion_allowed');
    expect(replaySessionWireFixture).not.toHaveProperty('deletion_target');
    expect(replaySessionWireFixture).not.toHaveProperty('deletion_error');
  });

  const jsonResponse = (body: unknown, status = 200) => new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json' },
  });

  const createApiStore = async (fetchMock: ReturnType<typeof vi.fn>) => {
    const NativeRequest = globalThis.Request;
    class AbsoluteUrlRequest extends NativeRequest {
      constructor(input: RequestInfo | URL, init?: RequestInit) {
        super(typeof input === 'string' && input.startsWith('/') ? `http://localhost${input}` : input, init);
      }
    }
    vi.stubGlobal('Request', AbsoluteUrlRequest);
    vi.stubGlobal('fetch', fetchMock);
    const { silApi } = await vi.importActual<typeof import('../../../api/silApi')>('../../../api/silApi');
    const store = configureStore({
      reducer: { [silApi.reducerPath]: silApi.reducer },
      middleware: (getDefaultMiddleware) => getDefaultMiddleware().concat(silApi.middleware),
    });
    return { silApi, store };
  };

  it('refetches once for changed scan results but not for rejected or unchanged scans', async () => {
    let scanResponse = jsonResponse({ detail: 'scan rejected' }, 500);
    let getCount = 0;
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        return jsonResponse({ sessions: [] });
      }
      return scanResponse;
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate());

    try {
      await subscription.unwrap();
      await expect(store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap())
        .rejects.toBeDefined();
      await new Promise((resolve) => window.setTimeout(resolve, 0));
      expect(getCount).toBe(1);

      scanResponse = jsonResponse({ ingested: 0, pruned: 0, errors: [] });
      await store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap();
      await new Promise((resolve) => window.setTimeout(resolve, 0));
      expect(getCount).toBe(1);

      scanResponse = jsonResponse({
        ingested: 1,
        pruned: 1,
        errors: [{ path: '/runs/broken', error: 'partial failure' }],
      });
      await store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap();
      await waitFor(() => expect(getCount).toBe(2));
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });

  it('refetches once after fulfilled deletion but not after rejected deletion', async () => {
    let deleteResponse = jsonResponse({ detail: 'delete rejected' }, 500);
    let getCount = 0;
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        return jsonResponse({ sessions: [] });
      }
      return deleteResponse;
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate());

    try {
      await subscription.unwrap();
      await expect(store.dispatch(silApi.endpoints.deleteEvidenceLibrarySession.initiate('evidence-1')).unwrap())
        .rejects.toBeDefined();
      await new Promise((resolve) => window.setTimeout(resolve, 0));
      expect(getCount).toBe(1);

      deleteResponse = jsonResponse({
        evidence_id: 'evidence-1',
        deleted_path: '/runs/evidence-1',
        filesystem_deleted: true,
      });
      await store.dispatch(silApi.endpoints.deleteEvidenceLibrarySession.initiate('evidence-1')).unwrap();
      await waitFor(() => expect(getCount).toBe(2));
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });

  it('preserves a scan refetch when overlapping delete is rejected', async () => {
    const prunedSession = {
      ...primarySession,
      evidence_id: 'scan-pruned-before-delete',
      session_id: '20260713_120000_scan_pruned_before_delete',
    };
    const events: string[] = [];
    let getCount = 0;
    let scanGetSignal: AbortSignal | undefined;
    let resolveScanGet!: (response: Response) => void;
    const scanGet = new Promise<Response>((resolve) => {
      resolveScanGet = resolve;
    });
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        if (getCount === 1) {
          events.push('GET-1 A+B');
          return jsonResponse({ sessions: [prunedSession, secondarySession] });
        }
        events.push('GET-2 pending');
        scanGetSignal = request.signal;
        return scanGet;
      }
      if (request.method === 'POST') {
        events.push('SCAN 200');
        return jsonResponse({ ingested: 1, pruned: 1, errors: [] });
      }
      events.push('DELETE A 404');
      queueMicrotask(() => {
        events.push('GET-2 B 200');
        resolveScanGet(jsonResponse({ sessions: [secondarySession] }));
      });
      return jsonResponse({ detail: 'session already pruned' }, 404);
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate());

    try {
      await subscription.unwrap();
      await store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap();
      await waitFor(() => expect(events).toEqual(['GET-1 A+B', 'SCAN 200', 'GET-2 pending']));

      await expect(store.dispatch(
        silApi.endpoints.deleteEvidenceLibrarySession.initiate(prunedSession.evidence_id),
      ).unwrap()).rejects.toBeDefined();

      await waitFor(() => expect(events).toEqual([
        'GET-1 A+B',
        'SCAN 200',
        'GET-2 pending',
        'DELETE A 404',
        'GET-2 B 200',
      ]));
      expect(scanGetSignal?.aborted).toBe(false);
      expect(getCount).toBe(2);
      await waitFor(() => {
        const query = silApi.endpoints.getEvidenceLibrarySessions.select()(store.getState());
        expect(query.isSuccess).toBe(true);
        expect(query.data?.sessions.map((session) => session.evidence_id)).toEqual([
          secondarySession.evidence_id,
        ]);
      });
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });

  it('removes a fulfilled deletion from cache even when invalidated refetch fails', async () => {
    let getCount = 0;
    let resolveRefetch!: (response: Response) => void;
    const pendingRefetch = new Promise<Response>((resolve) => {
      resolveRefetch = resolve;
    });
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        if (getCount === 1) return jsonResponse({ sessions: [primarySession, secondarySession] });
        return pendingRefetch;
      }
      return jsonResponse({
        evidence_id: primarySession.evidence_id,
        deleted_path: primarySession.deletion_target,
        filesystem_deleted: true,
      });
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate());

    try {
      await subscription.unwrap();
      await store.dispatch(
        silApi.endpoints.deleteEvidenceLibrarySession.initiate(primarySession.evidence_id),
      ).unwrap();
      await waitFor(() => expect(getCount).toBe(2));

      const pendingQuery = silApi.endpoints.getEvidenceLibrarySessions.select()(store.getState());
      expect(pendingQuery.data?.sessions.map((session) => session.evidence_id)).toEqual([
        secondarySession.evidence_id,
      ]);

      resolveRefetch(jsonResponse({ detail: 'stale list refresh failed' }, 500));
      await waitFor(() => {
        const query = silApi.endpoints.getEvidenceLibrarySessions.select()(store.getState());
        expect(query.isSuccess).toBe(true);
      });

      const query = silApi.endpoints.getEvidenceLibrarySessions.select()(store.getState());
      expect(query.data?.sessions.map((session) => session.evidence_id)).toEqual([
        secondarySession.evidence_id,
      ]);
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });

  it('aborts a mounted scan refetch before delete so stale success and failed invalidation cannot resurrect a row', async () => {
    const raceSession = {
      ...primarySession,
      evidence_id: 'mounted-race-delete',
      session_id: '20260710_120000_mounted_race_delete',
    };
    const events: string[] = [];
    let getCount = 0;
    let staleGetSignal: AbortSignal | undefined;
    let resolveStaleGet!: (response: Response) => void;
    const staleGet = new Promise<Response>((resolve) => {
      resolveStaleGet = resolve;
    });
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        if (getCount === 1) {
          events.push('GET-1 200');
          return jsonResponse({ sessions: [raceSession, secondarySession] });
        }
        if (getCount === 2) {
          events.push('GET-2 pending');
          staleGetSignal = request.signal;
          return staleGet;
        }
        events.push('GET-3 500');
        return jsonResponse({ detail: 'invalidation refresh failed' }, 500);
      }
      if (request.method === 'POST') {
        events.push('SCAN 200');
        return jsonResponse({ ingested: 1, pruned: 0, errors: [] });
      }
      events.push('DELETE 200');
      queueMicrotask(() => {
        events.push('GET-2 200');
        resolveStaleGet(jsonResponse({ sessions: [raceSession, secondarySession] }));
      });
      return jsonResponse({
        evidence_id: raceSession.evidence_id,
        deleted_path: raceSession.deletion_target,
        filesystem_deleted: true,
      });
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const MountedSessions = () => {
      const { data } = silApi.useGetEvidenceLibrarySessionsQuery();
      return data?.sessions.map((session) => (
        <button key={session.evidence_id} type="button" aria-label={`mounted delete ${session.evidence_id}`}>
          {session.evidence_id}
        </button>
      ));
    };
    const view = render(
      <Provider store={store}>
        <MountedSessions />
      </Provider>,
    );

    try {
      await screen.findByRole('button', { name: `mounted delete ${raceSession.evidence_id}` });
      expect(silApi.endpoints.getEvidenceLibrarySessions.select()(store.getState()).data?.sessions)
        .toHaveLength(2);

      await act(async () => {
        await store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap();
      });
      await waitFor(() => expect(events).toEqual(['GET-1 200', 'SCAN 200', 'GET-2 pending']));
      expect(staleGetSignal?.aborted).toBe(false);

      await act(async () => {
        await store.dispatch(
          silApi.endpoints.deleteEvidenceLibrarySession.initiate(raceSession.evidence_id),
        ).unwrap();
      });
      await waitFor(() => expect(staleGetSignal?.aborted).toBe(true));
      await waitFor(() => expect(events).toEqual([
        'GET-1 200',
        'SCAN 200',
        'GET-2 pending',
        'DELETE 200',
        'GET-2 200',
        'GET-3 500',
      ]));
      await waitFor(() => {
        const query = silApi.endpoints.getEvidenceLibrarySessions.select()(store.getState());
        expect(query.isSuccess).toBe(true);
      });

      const query = silApi.endpoints.getEvidenceLibrarySessions.select()(store.getState());
      expect(query.data?.sessions.map((session) => session.evidence_id)).toEqual([
        secondarySession.evidence_id,
      ]);
      await waitFor(() => {
        expect(screen.queryByRole('button', { name: `mounted delete ${raceSession.evidence_id}` }))
          .not.toBeInTheDocument();
      });
      expect(screen.getByRole('button', { name: `mounted delete ${secondarySession.evidence_id}` }))
        .toBeEnabled();
    } finally {
      view.unmount();
      vi.unstubAllGlobals();
    }
  });

  it('shows a legally rebuilt session with the same evidence ID after scan and list refresh', async () => {
    const rebuiltSession = {
      ...primarySession,
      evidence_id: 'rebuilt-same-evidence-id',
      session_id: '20260710_130000_rebuilt_same_path',
    };
    let getCount = 0;
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        if (getCount === 1) return jsonResponse({ sessions: [rebuiltSession, secondarySession] });
        if (getCount === 2) return jsonResponse({ sessions: [secondarySession] });
        return jsonResponse({ sessions: [rebuiltSession, secondarySession] });
      }
      if (request.method === 'DELETE') {
        return jsonResponse({
          evidence_id: rebuiltSession.evidence_id,
          deleted_path: rebuiltSession.deletion_target,
          filesystem_deleted: true,
        });
      }
      return jsonResponse({ ingested: 1, pruned: 0, errors: [] });
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate());

    try {
      await subscription.unwrap();
      await store.dispatch(
        silApi.endpoints.deleteEvidenceLibrarySession.initiate(rebuiltSession.evidence_id),
      ).unwrap();
      await waitFor(() => expect(getCount).toBe(2));
      expect(silApi.endpoints.getEvidenceLibrarySessions.select()(store.getState()).data?.sessions
        .map((session) => session.evidence_id)).toEqual([secondarySession.evidence_id]);

      await store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap();
      await waitFor(() => expect(getCount).toBe(3));
      expect(silApi.endpoints.getEvidenceLibrarySessions.select()(store.getState()).data?.sessions
        .map((session) => session.evidence_id)).toEqual([
        rebuiltSession.evidence_id,
        secondarySession.evidence_id,
      ]);
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });
});
