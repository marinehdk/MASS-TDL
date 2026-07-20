import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { configureStore } from '@reduxjs/toolkit';
import { Provider } from 'react-redux';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { EvidenceLibrarySessionsQuery, EvidenceReplaySession } from '../../../api/silApi';
import { EvidenceLibraryView } from '../EvidenceLibraryView';

const apiMocks = vi.hoisted(() => ({
  sessions: [] as Array<Record<string, unknown>>,
  sessionsResponseOverride: null as Record<string, unknown> | null,
  lastSessionsQuery: null as EvidenceLibrarySessionsQuery | null,
  sessionsQueryHistory: [] as EvidenceLibrarySessionsQuery[],
  sessionsIsFetching: false,
  sessionsIsLoading: false,
  sessionsHasData: true,
  sessionsIsError: false,
  sessionsError: null as unknown,
  rescan: vi.fn(),
  rescanUnwrap: vi.fn(),
  rescanIsLoading: false,
  getRescanStatus: vi.fn(),
  getRescanStatusUnwrap: vi.fn(),
  refetch: vi.fn(),
  refetchUnwrap: vi.fn(),
  deleteSession: vi.fn(),
  deleteUnwrap: vi.fn(),
  deleteIsLoading: false,
  batchDeleteSessions: vi.fn(),
  batchDeleteUnwrap: vi.fn(),
  batchDeleteIsLoading: false,
  configIdentity: {
    config_home: '/tmp/config-primary',
    database_path: '/tmp/config-primary/evidence-index.sqlite',
    roots: [{ root_id: 'worktrees', path_glob: '/tmp/.worktrees/*/runs/*/trace' }],
  } as Record<string, unknown>,
  configFetch: vi.fn(),
}));

const defaultSessionsQuery: EvidenceLibrarySessionsQuery = {
  page: 1,
  page_size: 20,
  sort_key: 'time',
  sort_direction: 'desc',
};

vi.mock('../../../api/silApi', () => ({
  useGetEvidenceLibrarySessionsQuery: (query?: EvidenceLibrarySessionsQuery) => {
    apiMocks.lastSessionsQuery = query ?? null;
    if (query) apiMocks.sessionsQueryHistory.push(query);
    return {
    data: apiMocks.sessionsHasData
      ? apiMocks.sessionsResponseOverride ?? buildSessionsResponse(query)
      : undefined,
    isLoading: apiMocks.sessionsIsLoading,
    isFetching: apiMocks.sessionsIsFetching,
    isError: apiMocks.sessionsIsError,
    error: apiMocks.sessionsError,
    refetch: apiMocks.refetch,
  };
  },
  useRescanEvidenceLibraryMutation: () => [apiMocks.rescan, { isLoading: apiMocks.rescanIsLoading }],
  useLazyGetEvidenceLibraryRescanStatusQuery: () => [apiMocks.getRescanStatus],
  useDeleteEvidenceLibrarySessionMutation: () => [
    apiMocks.deleteSession,
    { isLoading: apiMocks.deleteIsLoading, error: null },
  ],
  useBatchDeleteEvidenceLibrarySessionsMutation: () => [
    apiMocks.batchDeleteSessions,
    { isLoading: apiMocks.batchDeleteIsLoading, error: null },
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

const sessionScenarioCount = (session: Record<string, unknown>) => (
  Array.isArray(session.scenario_ids) ? session.scenario_ids.length : Number(session.scenario_count ?? 0)
);

const sessionOutcome = (session: Record<string, unknown>) => {
  const count = sessionScenarioCount(session);
  const passed = Number(session.passed_scenarios ?? 0);
  const failed = Number(session.failed_scenarios ?? 0);
  if (failed > 0) return 'failed';
  if (count > 0 && passed === count) return 'passed';
  return 'unknown';
};

const sessionMode = (session: Record<string, unknown>) => {
  const text = `${session.session_id ?? ''} ${session.suite ?? ''}`.toLowerCase();
  if (['debug', 'dbg', 'trace', 'ctx'].some((marker) => text.includes(marker))) return 'debug';
  if (text.includes('cohort')) return 'cohort';
  if (['full', 'clean8', 'clean12'].some((marker) => text.includes(marker))) return 'full';
  if (text.includes('fast') || session.suite === 'single') return 'avoidance';
  return 'debug';
};

const modeLabels: Record<string, string> = {
  debug: '调试验证',
  cohort: '同类验证',
  full: '完整验证',
  avoidance: '避碰验证',
};
const outcomeLabels: Record<string, string> = { passed: '通过', failed: '不通过', unknown: '-' };
const sourceLabels: Record<string, string> = { cli: 'CLI', front: 'Front' };

const sessionSource = (session: Record<string, unknown>) => {
  const raw = String(session.source ?? '');
  const canonical = ['frontend', 'front'].includes(raw.toLowerCase()) ? 'front' : raw.toLowerCase() || '-';
  return { value: canonical, label: sourceLabels[canonical] ?? (raw || '-') };
};

const sessionScenario = (session: Record<string, unknown>) => {
  const scenarios = Array.isArray(session.scenario_ids) ? session.scenario_ids.map(String) : [];
  if (scenarios.length === 0) return '-';
  if (scenarios.length <= 2) return scenarios.join(', ');
  return `${scenarios[0]} +${scenarios.length - 1}`;
};

const sessionWorktree = (session: Record<string, unknown>) => {
  if (sessionSource(session).label === 'Front') return '';
  if (session.worktree_name) return String(session.worktree_name);
  const match = String(session.session_path ?? '').match(/\/\.worktrees\/([^/]+)\//);
  return match?.[1] ?? String(session.branch ?? '-');
};

const makeFacet = (
  sessions: Array<Record<string, unknown>>,
  valueOf: (session: Record<string, unknown>) => string,
  labelOf: (value: string) => string = (value) => value,
) => Array.from(new Set(sessions.map(valueOf).filter((value) => value !== '')))
  .sort((left, right) => left.localeCompare(right, 'zh-Hans-CN', { numeric: true }))
  .map((value) => ({
    value,
    label: labelOf(value),
    count: sessions.filter((session) => valueOf(session) === value).length,
  }));

const buildSessionsResponse = (query?: EvidenceLibrarySessionsQuery) => {
  if (!query) return { sessions: apiMocks.sessions };
  const facets = {
    result: makeFacet(apiMocks.sessions, sessionOutcome, (value) => outcomeLabels[value]),
    scenarioCount: makeFacet(apiMocks.sessions, (session) => String(sessionScenarioCount(session))),
    mode: makeFacet(apiMocks.sessions, sessionMode, (value) => modeLabels[value]),
    scenario: makeFacet(apiMocks.sessions, sessionScenario),
    source: makeFacet(apiMocks.sessions, (session) => sessionSource(session).value, (value) => sourceLabels[value] ?? value),
    worktree: makeFacet(apiMocks.sessions, sessionWorktree),
  };
  const search = query.search?.trim().toLocaleLowerCase() ?? '';
  const filtered = apiMocks.sessions.filter((session) => {
    const matchesSearch = !search || [
      session.evidence_id,
      session.session_id,
      sessionScenario(session),
      ...(Array.isArray(session.scenario_ids) ? session.scenario_ids : []),
      sessionSource(session).label,
      session.source,
      session.suite,
      sessionMode(session),
      modeLabels[sessionMode(session)],
      sessionWorktree(session),
      session.worktree_name,
      session.branch,
      sessionOutcome(session),
      outcomeLabels[sessionOutcome(session)],
    ].some((value) => String(value ?? '').toLocaleLowerCase().includes(search));
    return matchesSearch
      && (!query.result || sessionOutcome(session) === query.result)
      && (query.scenario_count === undefined || sessionScenarioCount(session) === query.scenario_count)
      && (!query.mode || sessionMode(session) === query.mode)
      && (!query.scenario || sessionScenario(session) === query.scenario)
      && (!query.source || sessionSource(session).value === query.source)
      && (!query.worktree || sessionWorktree(session) === query.worktree);
  });
  const sortValue = (session: Record<string, unknown>) => {
    if (query.sort_key === 'time') return String(session.created_at ?? session.ended_at ?? session.session_id ?? '');
    if (query.sort_key === 'result') return outcomeLabels[sessionOutcome(session)];
    if (query.sort_key === 'scenarioCount') return sessionScenarioCount(session);
    if (query.sort_key === 'mode') return modeLabels[sessionMode(session)];
    if (query.sort_key === 'scenario') return sessionScenario(session);
    if (query.sort_key === 'source') return sessionSource(session).label;
    return sessionWorktree(session);
  };
  filtered.sort((left, right) => String(left.evidence_id).localeCompare(String(right.evidence_id)));
  filtered.sort((left, right) => {
    const leftValue = sortValue(left);
    const rightValue = sortValue(right);
    const result = typeof leftValue === 'number' && typeof rightValue === 'number'
      ? leftValue - rightValue
      : String(leftValue).localeCompare(String(rightValue), 'zh-Hans-CN', { numeric: true });
    return query.sort_direction === 'asc' ? result : -result;
  });
  const totalPages = Math.max(1, Math.ceil(filtered.length / query.page_size));
  const page = Math.min(Math.max(query.page, 1), totalPages);
  const offset = (page - 1) * query.page_size;
  return {
    sessions: filtered.slice(offset, offset + query.page_size),
    total: apiMocks.sessions.length,
    filtered_total: filtered.length,
    page,
    page_size: query.page_size,
    total_pages: totalPages,
    facets,
  };
};

const deleteButton = (sessionId = primarySession.session_id) =>
  screen.getByRole('button', { name: `删除 ${sessionId}` });

const pendingCleanupStorageKeys = () => Array.from(
  { length: window.localStorage.length },
  (_, index) => window.localStorage.key(index),
).filter((key): key is string => Boolean(
  key?.startsWith('mass-l3:evidence-library:pending-cleanup:v1:'),
));

const configIdentityResponse = () => new Response(
  JSON.stringify(apiMocks.configIdentity),
  { status: 200, headers: { 'Content-Type': 'application/json' } },
);

beforeEach(() => {
  window.localStorage.clear();
  apiMocks.sessions = [{ ...primarySession }, { ...secondarySession }];
  apiMocks.sessionsResponseOverride = null;
  apiMocks.lastSessionsQuery = null;
  apiMocks.sessionsQueryHistory = [];
  apiMocks.sessionsIsFetching = false;
  apiMocks.sessionsIsLoading = false;
  apiMocks.sessionsHasData = true;
  apiMocks.sessionsIsError = false;
  apiMocks.sessionsError = null;
  apiMocks.rescan.mockReset();
  apiMocks.rescanUnwrap.mockReset();
  apiMocks.rescanIsLoading = false;
  apiMocks.getRescanStatus.mockReset();
  apiMocks.getRescanStatusUnwrap.mockReset();
  apiMocks.refetch.mockReset();
  apiMocks.refetchUnwrap.mockReset();
  apiMocks.deleteSession.mockReset();
  apiMocks.deleteUnwrap.mockReset();
  apiMocks.deleteIsLoading = false;
  apiMocks.batchDeleteSessions.mockReset();
  apiMocks.batchDeleteUnwrap.mockReset();
  apiMocks.batchDeleteIsLoading = false;
  apiMocks.configIdentity = {
    config_home: '/tmp/config-primary',
    database_path: '/tmp/config-primary/evidence-index.sqlite',
    roots: [{ root_id: 'worktrees', path_glob: '/tmp/.worktrees/*/runs/*/trace' }],
  };
  apiMocks.configFetch.mockReset();
  apiMocks.configFetch.mockImplementation(async () => configIdentityResponse());
  vi.stubGlobal('fetch', apiMocks.configFetch);
  apiMocks.rescanUnwrap.mockResolvedValue({ ingested: 1, pruned: 0, errors: [] });
  apiMocks.rescan.mockReturnValue({ unwrap: apiMocks.rescanUnwrap });
  apiMocks.getRescanStatus.mockReturnValue({ unwrap: apiMocks.getRescanStatusUnwrap });
  apiMocks.refetchUnwrap.mockResolvedValue(undefined);
  apiMocks.refetch.mockReturnValue({ unwrap: apiMocks.refetchUnwrap });
  apiMocks.deleteUnwrap.mockResolvedValue({
    evidence_id: primarySession.evidence_id,
    deleted_path: primarySession.deletion_target,
    filesystem_deleted: true,
  });
  apiMocks.deleteSession.mockReturnValue({ unwrap: apiMocks.deleteUnwrap });
  apiMocks.batchDeleteUnwrap.mockResolvedValue({
    requested: 0,
    deleted: 0,
    failed: 0,
    results: [],
  });
  apiMocks.batchDeleteSessions.mockReturnValue({ unwrap: apiMocks.batchDeleteUnwrap });
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

  it('uses manual scans only', async () => {
    vi.useFakeTimers();
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);

    try {
      expect(screen.queryByRole('combobox', { name: '自动刷新间隔' })).not.toBeInTheDocument();
      expect(screen.queryByLabelText('距离下次扫描')).not.toBeInTheDocument();
      act(() => vi.advanceTimersByTime(86_400_000));
      expect(apiMocks.rescan).not.toHaveBeenCalled();

      vi.useRealTimers();
      fireEvent.click(screen.getByRole('button', { name: '扫描' }));
      await waitFor(() => expect(apiMocks.rescan).toHaveBeenCalledTimes(1));
    } finally {
      view.unmount();
      vi.useRealTimers();
    }
  });

  it('forces an authoritative list refresh after a successful zero-change scan', async () => {
    apiMocks.rescanUnwrap.mockResolvedValueOnce({ ingested: 0, pruned: 0, errors: [] });
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    expect(apiMocks.rescan).toHaveBeenCalledWith({ force: false });
    await waitFor(() => expect(apiMocks.rescanUnwrap).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(apiMocks.refetch).toHaveBeenCalledTimes(1));
    expect(apiMocks.refetchUnwrap).toHaveBeenCalledTimes(1);
  });

  it('retains loaded rows, polls progress, and refetches once after completion', async () => {
    apiMocks.rescanUnwrap.mockResolvedValueOnce({
      job_id: 'job-progress',
      state: 'queued',
      force: false,
      total: 2,
      processed: 0,
      ingested: 0,
      skipped: 0,
      pruned: 0,
      errors: [],
      cleanup_pending: [],
      started_at: null,
      finished_at: null,
    });
    apiMocks.getRescanStatusUnwrap
      .mockResolvedValueOnce({
        job_id: 'job-progress',
        state: 'running',
        force: false,
        total: 2,
        processed: 1,
        ingested: 1,
        skipped: 0,
        pruned: 0,
        errors: [],
        cleanup_pending: [],
        started_at: '2026-07-20T00:00:00Z',
        finished_at: null,
      })
      .mockResolvedValueOnce({
        job_id: 'job-progress',
        state: 'completed',
        force: false,
        total: 2,
        processed: 2,
        ingested: 1,
        skipped: 1,
        pruned: 0,
        errors: [],
        cleanup_pending: [],
        started_at: '2026-07-20T00:00:00Z',
        finished_at: '2026-07-20T00:00:01Z',
      });
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
    await waitFor(() => expect(apiMocks.getRescanStatusUnwrap).toHaveBeenCalledTimes(1));
    expect(screen.getByRole('button', { name: '扫描 1/2' })).toBeDisabled();
    expect(screen.getByText('colreg-rule14-ho')).toBeInTheDocument();
    await waitFor(() => expect(apiMocks.getRescanStatusUnwrap).toHaveBeenCalledTimes(2));
    await waitFor(() => expect(apiMocks.refetch).toHaveBeenCalledTimes(1));
    expect(apiMocks.refetchUnwrap).toHaveBeenCalledTimes(1);
  });

  it('stops polling and exposes worker failure without refetching sessions', async () => {
    apiMocks.rescanUnwrap.mockResolvedValueOnce({
      job_id: 'job-failed',
      state: 'queued',
      force: false,
      total: 0,
      processed: 0,
      ingested: 0,
      skipped: 0,
      pruned: 0,
      errors: [],
      cleanup_pending: [],
      started_at: null,
      finished_at: null,
    });
    apiMocks.getRescanStatusUnwrap.mockResolvedValueOnce({
      job_id: 'job-failed',
      state: 'failed',
      force: false,
      total: 0,
      processed: 0,
      ingested: 0,
      skipped: 0,
      pruned: 0,
      errors: [{ path: 'rescan', error: 'scan exploded' }],
      cleanup_pending: [],
      started_at: '2026-07-20T00:00:00Z',
      finished_at: '2026-07-20T00:00:01Z',
    });
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    await waitFor(() => expect(screen.getByText(/scan exploded/)).toBeInTheDocument());
    expect(apiMocks.getRescanStatusUnwrap).toHaveBeenCalledTimes(1);
    expect(apiMocks.refetch).not.toHaveBeenCalled();
  });

  it('stops polling and re-enables scanning after a status request error', async () => {
    apiMocks.rescanUnwrap.mockResolvedValueOnce({
      job_id: 'job-status-error',
      state: 'queued',
      force: false,
      total: 0,
      processed: 0,
      ingested: 0,
      skipped: 0,
      pruned: 0,
      errors: [],
      cleanup_pending: [],
      started_at: null,
      finished_at: null,
    });
    apiMocks.getRescanStatusUnwrap.mockRejectedValueOnce(new Error('status unavailable'));
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    await waitFor(() => expect(screen.getByRole('button', { name: '扫描' })).toBeEnabled());
    expect(screen.getAllByText(/扫描失败/).length).toBeGreaterThan(0);
    expect(apiMocks.getRescanStatusUnwrap).toHaveBeenCalledTimes(1);
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
    expect(apiMocks.refetch).toHaveBeenCalledTimes(1);
    expect(apiMocks.refetchUnwrap).toHaveBeenCalledTimes(1);
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

  it.each([
    ['ID', 'fedcba98'],
    ['scenario', 'rule15'],
    ['source', 'frontend'],
    ['worktree', 'evidence-library-replay-impl'],
    ['suite', 'clean8'],
    ['mode', '完整验证'],
    ['result', '不通过'],
  ])('filters rows by %s search text', async (_dimension, query) => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.change(screen.getByRole('searchbox', { name: '筛选仿真记录' }), {
      target: { value: query },
    });

    await waitFor(() => {
      expect(deleteButton(secondarySession.session_id)).toBeInTheDocument();
      expect(screen.queryByRole('button', { name: `删除 ${primarySession.session_id}` })).not.toBeInTheDocument();
    });
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

  it('uses present empty scenario ids as zero for display, sorting, filtering, and replay availability', () => {
    const emptyIndexedSession = {
      ...primarySession,
      evidence_id: 'count-empty',
      session_id: 'count_empty',
      source: 'empty',
      scenario_count: 12,
      passed_scenarios: 0,
      scenario_ids: [],
    };
    apiMocks.sessions = [
      emptyIndexedSession,
      { ...secondarySession, evidence_id: 'count-one', session_id: 'count_one', scenario_count: 1, scenario_ids: ['one'] },
    ];
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    const emptyRow = screen.getByRole('button', { name: '删除 count_empty' }).closest('tr');
    expect(emptyRow).not.toBeNull();
    expect(within(emptyRow!).getByText('0')).toBeInTheDocument();
    expect(within(emptyRow!).getByRole('button', { name: '回放' })).toBeDisabled();

    fireEvent.click(screen.getByRole('button', { name: '按场景数量升序' }));
    expect(screen.getAllByRole('row')[1]).toHaveTextContent('empty');

    fireEvent.click(screen.getByRole('button', { name: '筛选场景数量' }));
    const menu = screen.getByRole('menu', { name: '场景数量筛选选项' });
    expect(within(menu).getAllByRole('menuitem').map((item) => item.textContent)).toEqual(['全部', '0', '1']);
    fireEvent.click(within(menu).getByRole('menuitem', { name: '0' }));
    expect(screen.getAllByRole('row')).toHaveLength(2);
    expect(screen.getByRole('button', { name: '删除 count_empty' })).toBeInTheDocument();
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

  it('renders server totals and pages while rendering only the current 20 rows', () => {
    apiMocks.sessions = makeSessions(20);
    apiMocks.sessionsResponseOverride = {
      ...buildSessionsResponse({
        page: 1,
        page_size: 20,
        sort_key: 'time',
        sort_direction: 'desc',
      }),
      total: 313,
      filtered_total: 313,
      page: 1,
      page_size: 20,
      total_pages: 16,
    };

    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getByText('记录数: 313')).toBeInTheDocument();
    expect(screen.getByText('显示: 313')).toBeInTheDocument();
    expect(screen.getByText('1 / 16')).toBeInTheDocument();
    expect(screen.getAllByRole('button', { name: /^删除 page_session_/ })).toHaveLength(20);
    expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 1, page_size: 20 });
  });

  it('shows an explicit initial list error instead of false zero totals', () => {
    apiMocks.sessionsHasData = false;
    apiMocks.sessionsIsError = true;
    apiMocks.sessionsError = { status: 503, data: { detail: 'index unavailable' } };

    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getByRole('alert')).toHaveTextContent('证据列表加载失败');
    expect(screen.getByRole('alert')).toHaveTextContent('503');
    expect(screen.queryByText('记录数: 0')).not.toBeInTheDocument();
    expect(screen.queryByText('显示: 0')).not.toBeInTheDocument();
  });

  it('keeps the last page visible and reports a background refresh error', () => {
    apiMocks.sessions = makeSessions(20);
    apiMocks.sessionsIsError = true;
    apiMocks.sessionsError = { status: 500, data: { detail: 'refresh failed' } };

    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getAllByRole('button', { name: /^删除 page_session_/ })).toHaveLength(20);
    expect(screen.getByText('记录数: 20')).toBeInTheDocument();
    expect(screen.getByRole('alert')).toHaveTextContent('证据列表刷新失败，当前显示上次结果');
    expect(screen.getByRole('alert')).toHaveTextContent('500');
  });

  it('requests the next server page without slicing current rows locally', () => {
    apiMocks.sessions = makeSessions(41);
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '下一页' }));

    expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 2, page_size: 20 });
    expect(screen.getAllByRole('button', { name: /^删除 page_session_/ })).toHaveLength(20);
  });

  it('resets page for page size, sort, and canonical filters', () => {
    apiMocks.sessions = makeSessions(41);
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '下一页' }));
    fireEvent.change(screen.getByRole('combobox', { name: '每页记录数' }), { target: { value: '50' } });
    expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 1, page_size: 50 });

    fireEvent.click(screen.getByRole('button', { name: '按仿真时间升序' }));
    expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 1, sort_key: 'time', sort_direction: 'asc' });

    fireEvent.click(screen.getByRole('button', { name: '筛选模式' }));
    fireEvent.click(screen.getByRole('menuitem', { name: '避碰验证' }));
    expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 1, mode: 'avoidance' });

    fireEvent.click(screen.getByRole('button', { name: '筛选场景数量' }));
    fireEvent.click(screen.getByRole('menuitem', { name: '1' }));
    expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 1, scenario_count: 1 });
  });

  it('debounces server search for 250 ms and resets its page', () => {
    vi.useFakeTimers();
    apiMocks.sessions = makeSessions(41);
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);

    try {
      fireEvent.click(screen.getByRole('button', { name: '下一页' }));
      fireEvent.change(screen.getByRole('searchbox', { name: '筛选仿真记录' }), {
        target: { value: 'page_session_00' },
      });
      act(() => vi.advanceTimersByTime(249));
      expect(apiMocks.lastSessionsQuery?.search).toBeUndefined();

      act(() => vi.advanceTimersByTime(1));
      expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 1, search: 'page_session_00' });
    } finally {
      view.unmount();
      vi.useRealTimers();
    }
  });

  it('renders global facet options absent from the current page and sends canonical values', () => {
    apiMocks.sessions = makeSessions(20);
    apiMocks.sessionsResponseOverride = {
      ...buildSessionsResponse({
        page: 1,
        page_size: 20,
        sort_key: 'time',
        sort_direction: 'desc',
      }),
      facets: {
        result: [],
        scenarioCount: [],
        mode: [
          { value: 'avoidance', label: '避碰验证', count: 20 },
          { value: 'full', label: '完整验证', count: 1 },
        ],
        scenario: [],
        source: [],
        worktree: [],
      },
    };
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '筛选模式' }));
    fireEvent.click(screen.getByRole('menuitem', { name: '完整验证' }));

    expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 1, mode: 'full' });
  });

  it('accepts a backend-normalized page after the requested page shrinks', () => {
    apiMocks.sessions = makeSessions(41);
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: '下一页' }));
    fireEvent.click(screen.getByRole('button', { name: '下一页' }));
    expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 3 });

    apiMocks.sessionsResponseOverride = {
      ...buildSessionsResponse({
        page: 2,
        page_size: 20,
        sort_key: 'time',
        sort_direction: 'desc',
      }),
      page: 2,
      total_pages: 2,
    };
    view.rerender(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getByText('2 / 2')).toBeInTheDocument();
    expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 2 });
  });

  it('retains the previous page while the next page is fetching', () => {
    apiMocks.sessions = makeSessions(41);
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const previousFirstRow = screen.getAllByRole('row')[1].textContent;

    apiMocks.sessionsIsFetching = true;
    apiMocks.sessionsResponseOverride = buildSessionsResponse({
      page: 1,
      page_size: 20,
      sort_key: 'time',
      sort_direction: 'desc',
    });
    fireEvent.click(screen.getByRole('button', { name: '下一页' }));
    view.rerender(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.queryByText('Loading evidence')).not.toBeInTheDocument();
    expect(screen.getAllByRole('row')[1]).toHaveTextContent(previousFirstRow ?? '');
    expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 2 });
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

  it('selects safe rows across pages and preserves prior-page snapshots', () => {
    apiMocks.sessions = [
      ...makeSessions(25),
      {
        ...primarySession,
        evidence_id: 'unsafe-session',
        session_id: 'unsafe_session',
        created_at: '2026-06-01T00:00:00Z',
        scenario_ids: ['unsafe-scenario'],
        deletion_allowed: false,
        deletion_target: null,
        deletion_error: 'unsafe evidence target',
      },
    ];
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    const headers = screen.getAllByRole('columnheader');
    expect(headers[0]).toContainElement(screen.getByRole('checkbox', { name: '选择当前页' }));
    expect(headers[1]).toHaveTextContent('序号');

    const selectPage = screen.getByRole('checkbox', { name: '选择当前页' });
    fireEvent.click(selectPage);
    expect(selectPage).toBeChecked();
    expect(screen.getByText('已选择 20 条')).toBeInTheDocument();

    const firstRowSelection = screen.getAllByRole('checkbox').find((checkbox) => checkbox !== selectPage);
    expect(firstRowSelection).toBeDefined();
    fireEvent.click(firstRowSelection!);
    expect(selectPage).not.toBeChecked();
    expect((selectPage as HTMLInputElement).indeterminate).toBe(true);

    fireEvent.click(screen.getByRole('button', { name: '下一页' }));
    expect(screen.getByText('已选择 19 条')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    expect(screen.getByText('已选择 24 条')).toBeInTheDocument();
    expect(screen.getByRole('checkbox', { name: /unsafe-scenario/ })).toBeDisabled();

    fireEvent.click(screen.getByRole('button', { name: '上一页' }));
    expect(screen.getByText('已选择 24 条')).toBeInTheDocument();
    expect(screen.getByRole('checkbox', { name: '选择当前页' })).not.toBeChecked();
  });

  it('shows cancel selection and hides select-all when every filtered safe row is selected', () => {
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));

    expect(screen.getByText('已选择 2 条')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: '选择全部 2 条筛选结果' })).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '取消选择' }));

    expect(screen.queryByText(/已选择 \d+ 条/)).not.toBeInTheDocument();
    expect(screen.getByRole('checkbox', { name: /colreg-rule14-ho/ })).not.toBeChecked();
    expect(screen.getByRole('checkbox', { name: /colreg-rule15-cs/ })).not.toBeChecked();
  });

  it('excludes a safe filtered-out row when selecting all filtered results', () => {
    apiMocks.sessions = [
      { ...primarySession, worktree_name: 'safe-pass-worktree' },
      { ...secondarySession, worktree_name: 'safe-fail-worktree' },
    ];
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '筛选仿真结果' }));
    fireEvent.click(screen.getByRole('menuitem', { name: '不通过' }));
    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));

    expect(screen.queryByRole('button', { name: '选择全部 1 条筛选结果' })).not.toBeInTheDocument();
    expect(screen.getByText('已选择 1 条')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: `删除 ${primarySession.session_id}` })).not.toBeInTheDocument();
  });

  it('uses canonical scenario-count outcomes for multi-scenario filtering', () => {
    const scenarios = Array.from({ length: 8 }, (_, index) => `scenario-${index + 1}`);
    apiMocks.sessions = [
      {
        ...primarySession,
        evidence_id: 'multi-failed',
        session_id: 'multi_failed',
        scenario_ids: scenarios,
        scenario_count: 8,
        passed_scenarios: 0,
        failed_scenarios: 8,
      },
      {
        ...primarySession,
        evidence_id: 'multi-partial',
        session_id: 'multi_partial',
        scenario_ids: scenarios.map((scenario) => `${scenario}-partial`),
        scenario_count: 8,
        passed_scenarios: 4,
        failed_scenarios: 4,
      },
      {
        ...primarySession,
        evidence_id: 'multi-passed',
        session_id: 'multi_passed',
        scenario_ids: scenarios.map((scenario) => `${scenario}-passed`),
        scenario_count: 8,
        passed_scenarios: 8,
        failed_scenarios: 0,
      },
      {
        ...primarySession,
        evidence_id: 'multi-unknown',
        session_id: 'multi_unknown',
        scenario_ids: scenarios.map((scenario) => `${scenario}-unknown`),
        scenario_count: 8,
        passed_scenarios: 4,
        failed_scenarios: 0,
      },
    ];
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '筛选仿真结果' }));
    const menu = screen.getByRole('menu', { name: '仿真结果筛选选项' });
    expect(within(menu).queryByRole('menuitem', { name: '0/8 通过' })).not.toBeInTheDocument();
    fireEvent.click(within(menu).getByRole('menuitem', { name: '不通过' }));

    expect(deleteButton('multi_failed')).toBeInTheDocument();
    expect(deleteButton('multi_partial')).toBeInTheDocument();
    expect(screen.queryByRole('button', { name: '删除 multi_passed' })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: '删除 multi_unknown' })).not.toBeInTheDocument();
  });

  it('clears selection when the result set or manual scan changes', async () => {
    apiMocks.sessions = makeSessions(25);
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    const selectCurrentPage = () => fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    const expectSelected = (count: number) => expect(screen.getByText(`已选择 ${count} 条`)).toBeInTheDocument();
    const expectCleared = () => expect(screen.queryByText(/已选择 \d+ 条/)).not.toBeInTheDocument();

    selectCurrentPage();
    expectSelected(20);
    fireEvent.change(screen.getByRole('searchbox', { name: '筛选仿真记录' }), { target: { value: 'page_session_00' } });
    expectCleared();
    fireEvent.change(screen.getByRole('searchbox', { name: '筛选仿真记录' }), { target: { value: '' } });

    selectCurrentPage();
    expectSelected(20);
    fireEvent.click(screen.getByRole('button', { name: '筛选仿真结果' }));
    fireEvent.click(screen.getByRole('menuitem', { name: '通过' }));
    expectCleared();

    selectCurrentPage();
    expectSelected(20);
    fireEvent.click(screen.getByRole('button', { name: '按仿真时间升序' }));
    expectCleared();

    selectCurrentPage();
    expectSelected(20);
    fireEvent.change(screen.getByRole('combobox', { name: '每页记录数' }), { target: { value: '50' } });
    expectCleared();

    selectCurrentPage();
    expectSelected(25);
    fireEvent.click(screen.getByRole('button', { name: '扫描' }));
    await waitFor(() => expect(apiMocks.rescan).toHaveBeenCalledTimes(1));
    expectCleared();
  });

  it('locks selection throughout a rescan and keeps refreshed query data unselected', async () => {
    let resolveRescan!: (value: unknown) => void;
    apiMocks.rescanUnwrap.mockReturnValueOnce(new Promise((resolve) => {
      resolveRescan = resolve;
    }));
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: /colreg-rule14-ho/ }));
    expect(screen.getByText('已选择 1 条')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    apiMocks.rescanIsLoading = true;
    apiMocks.sessions = [{ ...primarySession, worktree_name: 'refreshed-worktree' }];
    view.rerender(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.queryByText(/已选择 \d+ 条/)).not.toBeInTheDocument();
    expect(screen.getByRole('checkbox', { name: '选择当前页' })).toBeDisabled();
    expect(screen.getByRole('checkbox', { name: /colreg-rule14-ho/ })).toBeDisabled();

    await act(async () => resolveRescan({ ingested: 1, pruned: 0, errors: [] }));
    apiMocks.rescanIsLoading = false;
    view.rerender(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getByRole('checkbox', { name: /colreg-rule14-ho/ })).not.toBeDisabled();
    expect(screen.queryByText(/已选择 \d+ 条/)).not.toBeInTheDocument();
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

  it('formats timezone-less run time to whole seconds', () => {
    apiMocks.sessions = [{ ...primarySession, created_at: '2026-07-07T13:20:00.123' }];
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

  it('resets to the first page when search changes', async () => {
    apiMocks.sessions = makeSessions(25);
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '下一页' }));
    expect(screen.getByText('2 / 2')).toBeInTheDocument();
    fireEvent.change(screen.getByRole('searchbox', { name: '筛选仿真记录' }), {
      target: { value: 'page_session_00' },
    });

    await waitFor(() => {
      expect(screen.getByText('1 / 1')).toBeInTheDocument();
      expect(deleteButton('page_session_00')).toBeInTheDocument();
    });
  });

  it('batch confirmation summarizes mixed selected sessions and closes after complete success', async () => {
    const multiScenarios = Array.from({ length: 8 }, (_, index) => `batch-scenario-${index + 1}`);
    const failInSecondWorktree = {
      ...secondarySession,
      evidence_id: 'batch-fail-worktree-b',
      session_id: 'batch_fail_worktree_b',
      source: 'cli',
      worktree_name: 'worktree-b',
      scenario_count: 8,
      scenario_ids: multiScenarios,
      passed_scenarios: 0,
      failed_scenarios: 8,
    };
    const failWithoutWorktree = {
      ...secondarySession,
      evidence_id: 'batch-fail-front',
      session_id: 'batch_fail_front',
      worktree_name: null,
      scenario_count: 8,
      scenario_ids: multiScenarios.map((scenario) => `${scenario}-partial`),
      passed_scenarios: 4,
      failed_scenarios: 4,
    };
    const unknownInFirstWorktree = {
      ...primarySession,
      evidence_id: 'batch-unknown-worktree-a',
      session_id: 'batch_unknown_worktree_a',
      worktree_name: 'worktree-a',
      scenario_count: 8,
      scenario_ids: multiScenarios.map((scenario) => `${scenario}-unknown`),
      passed_scenarios: 4,
      failed_scenarios: 0,
    };
    apiMocks.sessions = [
      { ...primarySession, worktree_name: 'worktree-a' },
      failInSecondWorktree,
      failWithoutWorktree,
      unknownInFirstWorktree,
    ];
    apiMocks.batchDeleteUnwrap.mockResolvedValueOnce({
      requested: 4,
      deleted: 4,
      failed: 0,
      results: apiMocks.sessions.map((session) => ({
        evidence_id: session.evidence_id,
        deleted_path: session.deletion_target,
        filesystem_deleted: true,
        status: 'deleted',
      })),
    });
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    fireEvent.click(screen.getByRole('button', { name: '删除所选（4）' }));

    const dialog = screen.getByRole('dialog', { name: '批量删除仿真记录' });
    expect(dialog).toHaveTextContent('共 4 条');
    expect(dialog).toHaveTextContent('通过 1');
    expect(dialog).toHaveTextContent('不通过 2');
    expect(dialog).toHaveTextContent('未知 1');
    expect(dialog).toHaveTextContent('工作树 2');
    expect(dialog).toHaveTextContent(/数据库与文件系统永久删除/);

    fireEvent.click(within(dialog).getByRole('button', { name: '确认批量删除' }));

    await waitFor(() => expect(apiMocks.batchDeleteSessions).toHaveBeenCalledWith({
      evidence_ids: [...apiMocks.sessions]
        .sort((left, right) => String(left.evidence_id).localeCompare(String(right.evidence_id)))
        .sort((left, right) => Date.parse(String(right.created_at)) - Date.parse(String(left.created_at)))
        .map((session) => session.evidence_id),
    }));
    await waitFor(() => expect(screen.queryByRole('dialog', { name: '批量删除仿真记录' })).not.toBeInTheDocument());
    expect(screen.queryByText(/已选择 \d+ 条/)).not.toBeInTheDocument();
  });

  it('keeps selected IDs and confirmation metadata fixed across query refreshes', async () => {
    const originalSessions = [
      { ...primarySession, worktree_name: 'snapshot-worktree-a' },
      { ...secondarySession, worktree_name: 'snapshot-worktree-b' },
    ];
    apiMocks.sessions = originalSessions;
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    apiMocks.sessions = [
      {
        ...primarySession,
        worktree_name: 'mutated-worktree',
        passed_scenarios: 0,
        failed_scenarios: 1,
      },
      {
        ...primarySession,
        evidence_id: 'new-query-session',
        session_id: 'new_query_session',
        worktree_name: 'new-worktree',
      },
    ];
    view.rerender(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getByText('已选择 2 条')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '删除所选（2）' }));
    const dialog = screen.getByRole('dialog', { name: '批量删除仿真记录' });
    expect(dialog).toHaveTextContent('通过 1');
    expect(dialog).toHaveTextContent('不通过 1');
    expect(dialog).toHaveTextContent('工作树 2');

    fireEvent.click(within(dialog).getByRole('button', { name: '确认批量删除' }));
    await waitFor(() => expect(apiMocks.batchDeleteSessions).toHaveBeenCalledWith({
      evidence_ids: [...originalSessions]
        .sort((left, right) => Date.parse(right.created_at) - Date.parse(left.created_at))
        .map((session) => session.evidence_id),
    }));
  });

  it('marks a rejected batch response unknown and blocks destructive retry until scan', async () => {
    apiMocks.batchDeleteUnwrap.mockRejectedValueOnce(new Error('response lost'));
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    fireEvent.click(screen.getByRole('button', { name: '删除所选（2）' }));
    fireEvent.click(screen.getByRole('button', { name: '确认批量删除' }));

    const dialog = screen.getByRole('dialog', { name: '批量删除仿真记录' });
    expect(await within(dialog).findByRole('alert')).toHaveTextContent('未收到批量删除结果');
    expect(within(dialog).queryByRole('button', { name: '确认批量删除' })).not.toBeInTheDocument();
    expect(within(dialog).queryByRole('button', { name: /重试/ })).not.toBeInTheDocument();
    expect(apiMocks.batchDeleteSessions).toHaveBeenCalledTimes(1);

    fireEvent.click(within(dialog).getByRole('button', { name: '关闭' }));
    expect(screen.getByRole('alert')).toHaveTextContent('批量删除结果未知');
    expect(screen.getByRole('button', { name: '删除所选（2）' })).toBeDisabled();
    expect(deleteButton()).toBeDisabled();
    fireEvent.click(deleteButton());
    expect(apiMocks.deleteSession).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: '删除所选（2）' }));
    expect(apiMocks.batchDeleteSessions).toHaveBeenCalledTimes(1);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));
    await waitFor(() => expect(apiMocks.rescanUnwrap).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(screen.queryByText('批量删除结果未知')).not.toBeInTheDocument());
    expect(screen.queryByText(/已选择 \d+ 条/)).not.toBeInTheDocument();
  });

  it('keeps unknown deletion state until zero-change scan list refresh completes', async () => {
    let resolveRefresh!: () => void;
    apiMocks.batchDeleteUnwrap.mockRejectedValueOnce(new Error('response lost'));
    apiMocks.rescanUnwrap.mockResolvedValueOnce({ ingested: 0, pruned: 0, errors: [] });
    apiMocks.refetchUnwrap.mockReturnValueOnce(new Promise<void>((resolve) => {
      resolveRefresh = resolve;
    }));
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    fireEvent.click(screen.getByRole('button', { name: '删除所选（2）' }));
    fireEvent.click(screen.getByRole('button', { name: '确认批量删除' }));
    const dialog = screen.getByRole('dialog', { name: '批量删除仿真记录' });
    await within(dialog).findByRole('alert');
    fireEvent.click(within(dialog).getByRole('button', { name: '关闭' }));

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));
    await waitFor(() => expect(apiMocks.refetch).toHaveBeenCalledTimes(1));
    expect(screen.getByRole('alert')).toHaveTextContent('批量删除结果未知');
    expect(deleteButton()).toBeDisabled();

    await act(async () => resolveRefresh());
    await waitFor(() => expect(screen.queryByText('批量删除结果未知')).not.toBeInTheDocument());
    expect(deleteButton()).not.toBeDisabled();
  });

  it('reconciles a lost delete response when authoritative scan finds only pending cleanup', async () => {
    const cleanupResult = {
      evidence_id: primarySession.evidence_id,
      deleted_path: primarySession.deletion_target,
      filesystem_deleted: false,
      filesystem_cleanup: 'pending',
      cleanup_error: 'staged filesystem cleanup is pending',
      cleanup_path: '/runs/.evidence-library-delete-staging/delete-lost-response',
      cleanup_metadata_path: '/runs/.evidence-library-delete-staging/delete-lost-response.json',
    };
    apiMocks.batchDeleteUnwrap.mockRejectedValueOnce(new Error('response lost'));
    apiMocks.rescanUnwrap.mockResolvedValueOnce({
      ingested: 0,
      pruned: 0,
      errors: [],
      cleanup_pending: [cleanupResult],
    });
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    fireEvent.click(screen.getByRole('button', { name: '删除所选（2）' }));
    fireEvent.click(screen.getByRole('button', { name: '确认批量删除' }));
    const dialog = screen.getByRole('dialog', { name: '批量删除仿真记录' });
    await within(dialog).findByRole('alert');
    fireEvent.click(within(dialog).getByRole('button', { name: '关闭' }));
    expect(deleteButton()).toBeDisabled();

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    await waitFor(() => expect(screen.queryByText('批量删除结果未知')).not.toBeInTheDocument());
    expect(deleteButton()).not.toBeDisabled();
    expect(screen.getByRole('alert', { name: '待处理文件清理' })).toHaveTextContent(
      cleanupResult.cleanup_path,
    );
  });

  it.each([
    {
      name: 'scan reports item errors',
      scanResult: { ingested: 0, pruned: 0, errors: [{ path: '/runs/broken', error: 'unreadable' }] },
      refreshError: null,
    },
    {
      name: 'authoritative list refresh fails',
      scanResult: { ingested: 0, pruned: 0, errors: [] },
      refreshError: new Error('list unavailable'),
    },
  ])('keeps unknown deletion state when $name', async ({ scanResult, refreshError }) => {
    apiMocks.batchDeleteUnwrap.mockRejectedValueOnce(new Error('response lost'));
    apiMocks.rescanUnwrap.mockResolvedValueOnce(scanResult);
    if (refreshError) apiMocks.refetchUnwrap.mockRejectedValueOnce(refreshError);
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    fireEvent.click(screen.getByRole('button', { name: '删除所选（2）' }));
    fireEvent.click(screen.getByRole('button', { name: '确认批量删除' }));
    const dialog = screen.getByRole('dialog', { name: '批量删除仿真记录' });
    await within(dialog).findByRole('alert');
    fireEvent.click(within(dialog).getByRole('button', { name: '关闭' }));

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));
    await waitFor(() => expect(apiMocks.refetchUnwrap).toHaveBeenCalledTimes(1));
    expect(screen.getByText('批量删除结果未知。请扫描核对证据库后重新选择。')).toBeInTheDocument();
    expect(deleteButton()).toBeDisabled();
  });

  it('reports pending post-commit filesystem cleanup as deleted without offering retry', async () => {
    apiMocks.batchDeleteUnwrap.mockResolvedValueOnce({
      requested: 2,
      deleted: 2,
      failed: 0,
      results: [
        {
          evidence_id: primarySession.evidence_id,
          deleted_path: primarySession.deletion_target,
          filesystem_deleted: false,
          filesystem_cleanup: 'pending',
          cleanup_error: 'staged filesystem cleanup is pending',
          cleanup_path: '/runs/.evidence-library-delete-staging/cleanup-token',
          status: 'deleted',
        },
        {
          evidence_id: secondarySession.evidence_id,
          deleted_path: secondarySession.deletion_target,
          filesystem_deleted: true,
          filesystem_cleanup: 'completed',
          status: 'deleted',
        },
      ],
    });
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    fireEvent.click(screen.getByRole('button', { name: '删除所选（2）' }));
    fireEvent.click(screen.getByRole('button', { name: '确认批量删除' }));

    const dialog = await screen.findByRole('dialog', { name: '批量删除仿真记录' });
    expect(dialog).toHaveTextContent('已删除 2');
    expect(dialog).toHaveTextContent('文件清理待处理 1');
    expect(dialog).toHaveTextContent(primarySession.evidence_id);
    expect(dialog).toHaveTextContent('staged filesystem cleanup is pending');
    expect(dialog).toHaveTextContent('/runs/.evidence-library-delete-staging/cleanup-token');
    expect(within(dialog).queryByRole('button', { name: /重试/ })).not.toBeInTheDocument();
    expect(screen.queryByText(/已选择 \d+ 条/)).not.toBeInTheDocument();
  });

  it('keeps a single-delete cleanup path visible until explicitly acknowledged', async () => {
    apiMocks.deleteUnwrap.mockResolvedValueOnce({
      evidence_id: primarySession.evidence_id,
      deleted_path: primarySession.deletion_target,
      filesystem_deleted: false,
      filesystem_cleanup: 'pending',
      cleanup_error: 'staged filesystem cleanup is pending',
      cleanup_path: '/runs/.evidence-library-delete-staging/delete-single',
      cleanup_metadata_path: '/runs/.evidence-library-delete-staging/delete-single.json',
    });
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(deleteButton());
    fireEvent.click(screen.getByRole('button', { name: '确认删除' }));

    const cleanupAlert = await screen.findByRole('alert', { name: '待处理文件清理' });
    expect(cleanupAlert).toHaveTextContent(primarySession.evidence_id);
    expect(cleanupAlert).toHaveTextContent('/runs/.evidence-library-delete-staging/delete-single');
    expect(cleanupAlert).toHaveTextContent('/runs/.evidence-library-delete-staging/delete-single.json');
    expect(screen.queryByRole('dialog', { name: '删除仿真记录' })).not.toBeInTheDocument();

    fireEvent.click(within(cleanupAlert).getByRole('button', {
      name: `确认已记录 ${primarySession.evidence_id}`,
    }));
    expect(screen.queryByRole('alert', { name: '待处理文件清理' })).not.toBeInTheDocument();
  });

  it('restores a rescan-discovered cleanup notice after reload until acknowledgment', async () => {
    const rootSidecar = '/runs/.evidence-library-delete-staging/delete-recovered.json';
    const centralRecord = '/tmp/config-primary/.evidence-library-delete-recovery/delete-recovered.json';
    const cleanupResult = {
      evidence_id: primarySession.evidence_id,
      deleted_path: primarySession.deletion_target,
      filesystem_deleted: false,
      filesystem_cleanup: 'pending',
      cleanup_error: 'staged filesystem cleanup is pending',
      cleanup_path: '/runs/.evidence-library-delete-staging/delete-recovered',
      cleanup_metadata_path: centralRecord,
      cleanup_paths: [
        '/runs/.evidence-library-delete-staging/delete-recovered',
        rootSidecar,
        centralRecord,
      ],
    };
    apiMocks.rescanUnwrap.mockResolvedValueOnce({
      ingested: 0,
      pruned: 0,
      errors: [],
      cleanup_pending: [cleanupResult],
    });
    const firstView = render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));
    const discoveredAlert = await screen.findByRole('alert', { name: '待处理文件清理' });
    expect(discoveredAlert).toHaveTextContent(cleanupResult.cleanup_path);
    expect(discoveredAlert).toHaveTextContent(rootSidecar);
    await waitFor(() => expect(pendingCleanupStorageKeys()).toHaveLength(1));
    const [storageKey] = pendingCleanupStorageKeys();
    expect(storageKey).toContain(encodeURIComponent(String(apiMocks.configIdentity.database_path)));
    expect(window.localStorage.getItem(storageKey)).toContain(cleanupResult.cleanup_path);
    expect(window.localStorage.getItem('mass-l3:evidence-library:pending-cleanup:v1')).toBeNull();

    firstView.unmount();
    const reloadedView = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const restoredAlert = await screen.findByRole('alert', { name: '待处理文件清理' });
    expect(restoredAlert).toHaveTextContent(cleanupResult.cleanup_metadata_path);

    fireEvent.click(within(restoredAlert).getByRole('button', {
      name: `确认已记录 ${primarySession.evidence_id}`,
    }));
    await waitFor(() => expect(window.localStorage.getItem(storageKey)).toBe('[]'));
    reloadedView.unmount();

    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    await waitFor(() => expect(
      screen.queryByRole('alert', { name: '待处理文件清理' }),
    ).not.toBeInTheDocument());
  });

  it('quarantines persisted cleanup records with malformed cleanup paths', async () => {
    const seedView = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    await waitFor(() => expect(pendingCleanupStorageKeys()).toHaveLength(1));
    const [storageKey] = pendingCleanupStorageKeys();
    seedView.unmount();

    const validRecord = {
      evidence_id: primarySession.evidence_id,
      deleted_path: primarySession.deletion_target,
      filesystem_deleted: false,
      filesystem_cleanup: 'pending',
      cleanup_error: 'staged filesystem cleanup is pending',
      cleanup_path: '/runs/.evidence-library-delete-staging/delete-valid-storage',
      cleanup_metadata_path: '/tmp/config-primary/.evidence-library-delete-recovery/delete-valid-storage.json',
      cleanup_paths: [
        '/runs/.evidence-library-delete-staging/delete-valid-storage',
        '/runs/.evidence-library-delete-staging/delete-valid-storage.json',
      ],
    };
    window.localStorage.setItem(storageKey, JSON.stringify([
      validRecord,
      {
        ...validRecord,
        evidence_id: 'malformed-array-record',
        cleanup_path: '/runs/malformed-array-record',
        cleanup_paths: ['/runs/malformed-array-record', 42],
      },
      {
        ...validRecord,
        evidence_id: 'malformed-shape-record',
        cleanup_path: '/runs/malformed-shape-record',
        cleanup_paths: '/runs/not-an-array',
      },
      {
        ...validRecord,
        evidence_id: 'malformed-metadata-record',
        cleanup_path: '/runs/malformed-metadata-record',
        cleanup_metadata_path: { path: '/runs/not-a-string' },
      },
    ]));

    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    const cleanupAlert = await screen.findByRole('alert', { name: '待处理文件清理' });
    expect(cleanupAlert).toHaveTextContent(validRecord.cleanup_path);
    expect(cleanupAlert).toHaveTextContent(validRecord.cleanup_paths[1]);
    expect(cleanupAlert).not.toHaveTextContent('malformed-array-record');
    expect(cleanupAlert).not.toHaveTextContent('malformed-shape-record');
    expect(cleanupAlert).not.toHaveTextContent('malformed-metadata-record');
    await waitFor(() => expect(JSON.parse(window.localStorage.getItem(storageKey) ?? '[]')).toEqual([
      validRecord,
    ]));
  });

  it.each([
    {
      name: 'HTTP failure',
      firstFetch: async () => new Response('unavailable', { status: 503 }),
    },
    {
      name: 'malformed identity',
      firstFetch: async () => new Response(
        JSON.stringify({ config_home: null, database_path: 42, roots: 'invalid' }),
        { status: 200, headers: { 'Content-Type': 'application/json' } },
      ),
    },
    {
      name: 'fetch exception',
      firstFetch: async () => {
        throw new Error('temporary identity failure');
      },
    },
  ])('locks deletion after $name, retries through scan, and persists cleanup', async ({ firstFetch }) => {
    const cleanupResult = {
      evidence_id: primarySession.evidence_id,
      deleted_path: primarySession.deletion_target,
      filesystem_deleted: false,
      filesystem_cleanup: 'pending',
      cleanup_error: 'staged filesystem cleanup is pending',
      cleanup_path: '/runs/.evidence-library-delete-staging/delete-after-config-retry',
      cleanup_metadata_path: '/tmp/config-primary/.evidence-library-delete-recovery/delete-after-config-retry.json',
    };
    apiMocks.configFetch.mockReset();
    apiMocks.configFetch
      .mockImplementationOnce(firstFetch)
      .mockImplementation(async () => configIdentityResponse());
    apiMocks.rescanUnwrap.mockResolvedValueOnce({
      ingested: 0,
      pruned: 0,
      errors: [],
      cleanup_pending: [cleanupResult],
    });
    const firstView = render(<EvidenceLibraryView onOpen={vi.fn()} />);

    await waitFor(() => expect(apiMocks.configFetch).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(deleteButton()).toBeDisabled());
    expect(screen.getByRole('button', { name: '扫描' })).not.toBeDisabled();

    fireEvent.click(screen.getByRole('button', { name: '扫描' }));

    await screen.findByRole('alert', { name: '待处理文件清理' });
    await waitFor(() => expect(apiMocks.configFetch).toHaveBeenCalledTimes(2));
    await waitFor(() => expect(pendingCleanupStorageKeys()).toHaveLength(1));
    firstView.unmount();

    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const restoredAlert = await screen.findByRole('alert', { name: '待处理文件清理' });
    expect(restoredAlert).toHaveTextContent(cleanupResult.cleanup_path);
  });

  it('explains the close and scan recovery flow when single-delete persistence initialization fails', async () => {
    let resolveConfig!: (response: Response) => void;
    apiMocks.configFetch.mockReset();
    apiMocks.configFetch.mockReturnValue(new Promise<Response>((resolve) => {
      resolveConfig = resolve;
    }));
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(deleteButton());
    const dialog = screen.getByRole('dialog', { name: '删除仿真记录' });
    fireEvent.click(within(dialog).getByRole('button', { name: '确认删除' }));
    await act(async () => resolveConfig(new Response('unavailable', { status: 503 })));

    expect(await within(dialog).findByRole('alert')).toHaveTextContent(
      '清理记录持久化不可用，尚未发送删除请求。请关闭对话框，扫描恢复后重新发起删除。',
    );
    expect(within(dialog).getByRole('button', { name: '关闭' })).toBeEnabled();
    expect(within(dialog).getByRole('button', { name: '确认删除' })).toBeDisabled();
    expect(apiMocks.deleteSession).not.toHaveBeenCalled();
  });

  it('explains the close, scan, and reselection flow when batch persistence initialization fails', async () => {
    let resolveConfig!: (response: Response) => void;
    apiMocks.configFetch.mockReset();
    apiMocks.configFetch.mockReturnValue(new Promise<Response>((resolve) => {
      resolveConfig = resolve;
    }));
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    fireEvent.click(screen.getByRole('button', { name: '删除所选（2）' }));
    const dialog = screen.getByRole('dialog', { name: '批量删除仿真记录' });
    fireEvent.click(within(dialog).getByRole('button', { name: '确认批量删除' }));
    await act(async () => resolveConfig(new Response('unavailable', { status: 503 })));

    expect(await within(dialog).findByRole('alert')).toHaveTextContent(
      '清理记录持久化不可用，尚未发送批量删除请求。请关闭对话框，扫描恢复后重新选择并重试。',
    );
    expect(within(dialog).getByRole('button', { name: '关闭' })).toBeEnabled();
    expect(within(dialog).getByRole('button', { name: '确认批量删除' })).toBeDisabled();
    expect(apiMocks.batchDeleteSessions).not.toHaveBeenCalled();
  });

  it('locks deletion when the first runtime cleanup persistence write fails until scan recovery', async () => {
    const cleanupResult = {
      evidence_id: primarySession.evidence_id,
      deleted_path: primarySession.deletion_target,
      filesystem_deleted: false,
      filesystem_cleanup: 'pending',
      cleanup_error: 'staged filesystem cleanup is pending',
      cleanup_path: '/runs/.evidence-library-delete-staging/delete-runtime-storage-failure',
      cleanup_metadata_path: '/tmp/config-primary/.evidence-library-delete-recovery/delete-runtime-storage-failure.json',
    };
    apiMocks.rescanUnwrap.mockResolvedValue({
      ingested: 0,
      pruned: 0,
      errors: [],
      cleanup_pending: [cleanupResult],
    });
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    await waitFor(() => expect(pendingCleanupStorageKeys()).toHaveLength(1));
    const [storageKey] = pendingCleanupStorageKeys();
    const setItemSpy = vi.spyOn(Storage.prototype, 'setItem');

    try {
      setItemSpy.mockImplementationOnce(() => {
        throw new Error('runtime storage failure');
      });
      fireEvent.click(screen.getByRole('button', { name: '扫描' }));

      const cleanupAlert = await screen.findByRole('alert', { name: '待处理文件清理' });
      expect(cleanupAlert).toHaveTextContent(cleanupResult.cleanup_path);
      expect(await screen.findByText(
        '清理记录未能持久保存，删除已锁定。请扫描恢复后再执行删除。',
      )).toBeInTheDocument();
      expect(deleteButton()).toBeDisabled();

      fireEvent.click(screen.getByRole('button', { name: '扫描' }));

      await waitFor(() => expect(screen.queryByText(
        '清理记录未能持久保存，删除已锁定。请扫描恢复后再执行删除。',
      )).not.toBeInTheDocument());
      expect(deleteButton()).toBeEnabled();
      expect(window.localStorage.getItem(storageKey)).toContain(cleanupResult.cleanup_path);
    } finally {
      setItemSpy.mockRestore();
    }
  });

  it('does not resurrect an acknowledged notice when identity hydration resolves late', async () => {
    const cleanupResult = {
      evidence_id: primarySession.evidence_id,
      deleted_path: primarySession.deletion_target,
      filesystem_deleted: false,
      filesystem_cleanup: 'pending',
      cleanup_error: 'staged filesystem cleanup is pending',
      cleanup_path: '/runs/.evidence-library-delete-staging/delete-delayed-hydration',
      cleanup_metadata_path: '/tmp/config-primary/.evidence-library-delete-recovery/delete-delayed-hydration.json',
    };
    apiMocks.rescanUnwrap.mockResolvedValue({
      ingested: 0,
      pruned: 0,
      errors: [],
      cleanup_pending: [cleanupResult],
    });
    const seedView = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: '扫描' }));
    await screen.findByRole('alert', { name: '待处理文件清理' });
    await waitFor(() => expect(pendingCleanupStorageKeys()).toHaveLength(1));
    const [storageKey] = pendingCleanupStorageKeys();
    seedView.unmount();

    let resolveConfig!: (response: Response) => void;
    apiMocks.configFetch.mockReset();
    apiMocks.configFetch.mockReturnValue(new Promise<Response>((resolve) => {
      resolveConfig = resolve;
    }));
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: '扫描' }));
    const pendingAlert = await screen.findByRole('alert', { name: '待处理文件清理' });
    fireEvent.click(within(pendingAlert).getByRole('button', {
      name: `确认已记录 ${primarySession.evidence_id}`,
    }));
    expect(screen.queryByRole('alert', { name: '待处理文件清理' })).not.toBeInTheDocument();

    await act(async () => resolveConfig(configIdentityResponse()));

    await waitFor(() => expect(window.localStorage.getItem(storageKey)).toBe('[]'));
    expect(screen.queryByRole('alert', { name: '待处理文件清理' })).not.toBeInTheDocument();
  });

  it('isolates durable cleanup notices by backend evidence-library configuration', async () => {
    const primaryConfig = {
      config_home: '/tmp/config-primary',
      database_path: '/tmp/config-primary/evidence-index.sqlite',
      roots: [
        { root_id: 'primary', path_glob: '/tmp/repo/runs/*/trace', enabled: true, trusted: true },
        { root_id: 'worktrees', path_glob: '/tmp/repo/.worktrees/*/runs/*/trace', enabled: true, trusted: true },
      ],
    };
    const cleanupResult = {
      evidence_id: primarySession.evidence_id,
      deleted_path: primarySession.deletion_target,
      filesystem_deleted: false,
      filesystem_cleanup: 'pending',
      cleanup_error: 'staged filesystem cleanup is pending',
      cleanup_path: '/runs/.evidence-library-delete-staging/delete-config-scoped',
    };
    apiMocks.rescanUnwrap.mockResolvedValueOnce({
      ingested: 0,
      pruned: 0,
      errors: [],
      cleanup_pending: [cleanupResult],
    });
    apiMocks.configIdentity = primaryConfig;
    const primaryView = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    fireEvent.click(screen.getByRole('button', { name: '扫描' }));
    await screen.findByRole('alert', { name: '待处理文件清理' });
    await waitFor(() => expect(pendingCleanupStorageKeys()).toHaveLength(1));
    primaryView.unmount();

    apiMocks.configIdentity = {
      ...primaryConfig,
      roots: [{ root_id: 'other-worktree', path_glob: '/tmp/other/runs/*/trace' }],
    };
    const otherWorktreeView = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    await waitFor(() => expect(apiMocks.configFetch).toHaveBeenCalledTimes(2));
    expect(screen.queryByRole('alert', { name: '待处理文件清理' })).not.toBeInTheDocument();
    otherWorktreeView.unmount();

    apiMocks.configIdentity = {
      ...primaryConfig,
      database_path: '/tmp/config-primary/other-evidence-index.sqlite',
    };
    const otherBackendView = render(<EvidenceLibraryView onOpen={vi.fn()} />);
    await waitFor(() => expect(apiMocks.configFetch).toHaveBeenCalledTimes(3));
    expect(screen.queryByRole('alert', { name: '待处理文件清理' })).not.toBeInTheDocument();
    otherBackendView.unmount();

    apiMocks.configIdentity = {
      database_path: primaryConfig.database_path,
      config_home: primaryConfig.config_home,
      roots: [
        { trusted: true, enabled: true, path_glob: '/tmp/repo/.worktrees/*/runs/*/trace', root_id: 'worktrees' },
        { path_glob: '/tmp/repo/runs/*/trace', root_id: 'primary', trusted: true, enabled: true },
      ],
    };
    render(<EvidenceLibraryView onOpen={vi.fn()} />);
    const restoredAlert = await screen.findByRole('alert', { name: '待处理文件清理' });
    expect(restoredAlert).toHaveTextContent(cleanupResult.cleanup_path);
  });

  it('preserves pending cleanup paths across failed-item batch retries until acknowledgment', async () => {
    const cleanupPath = '/runs/.evidence-library-delete-staging/delete-batch';
    apiMocks.batchDeleteUnwrap
      .mockResolvedValueOnce({
        requested: 2,
        deleted: 1,
        failed: 1,
        results: [
          {
            evidence_id: primarySession.evidence_id,
            deleted_path: primarySession.deletion_target,
            filesystem_deleted: false,
            filesystem_cleanup: 'pending',
            cleanup_error: 'staged filesystem cleanup is pending',
            cleanup_path: cleanupPath,
            status: 'deleted',
          },
          {
            evidence_id: secondarySession.evidence_id,
            status: 'failed',
            error: 'filesystem operation failed',
          },
        ],
      })
      .mockResolvedValueOnce({
        requested: 1,
        deleted: 1,
        failed: 0,
        results: [{
          evidence_id: secondarySession.evidence_id,
          deleted_path: secondarySession.deletion_target,
          filesystem_deleted: true,
          filesystem_cleanup: 'completed',
          status: 'deleted',
        }],
      });
    render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    fireEvent.click(screen.getByRole('button', { name: '删除所选（2）' }));
    fireEvent.click(screen.getByRole('button', { name: '确认批量删除' }));

    const resultDialog = await screen.findByRole('dialog', { name: '批量删除仿真记录' });
    expect(resultDialog).toHaveTextContent(cleanupPath);
    fireEvent.click(within(resultDialog).getByRole('button', { name: '重试失败项（1）' }));

    await waitFor(() => expect(apiMocks.batchDeleteSessions).toHaveBeenNthCalledWith(2, {
      evidence_ids: [secondarySession.evidence_id],
    }));
    await waitFor(() => expect(screen.queryByRole('dialog', { name: '批量删除仿真记录' })).not.toBeInTheDocument());
    const cleanupAlert = screen.getByRole('alert', { name: '待处理文件清理' });
    expect(cleanupAlert).toHaveTextContent(cleanupPath);

    fireEvent.click(within(cleanupAlert).getByRole('button', {
      name: `确认已记录 ${primarySession.evidence_id}`,
    }));
    expect(screen.queryByText(cleanupPath)).not.toBeInTheDocument();
  });

  it('partial batch delete disables controls, preserves failed selection, and retries only failures', async () => {
    const allSessions = makeSessions(21);
    const selectedSessions = [...allSessions]
      .sort((left, right) => Date.parse(right.created_at) - Date.parse(left.created_at))
      .slice(0, 20);
    const failedSession = selectedSessions[9];
    const successfulSessions = selectedSessions.filter((session) => session.evidence_id !== failedSession.evidence_id);
    let resolveBatch!: (value: unknown) => void;
    const pendingBatch = new Promise((resolve) => {
      resolveBatch = resolve;
    });
    apiMocks.sessions = allSessions;
    apiMocks.batchDeleteUnwrap
      .mockReturnValueOnce(pendingBatch)
      .mockResolvedValueOnce({
        requested: 1,
        deleted: 1,
        failed: 0,
        results: [{
          evidence_id: failedSession.evidence_id,
          deleted_path: failedSession.deletion_target,
          filesystem_deleted: true,
          status: 'deleted',
        }],
      });
    const view = render(<EvidenceLibraryView onOpen={vi.fn()} />);

    fireEvent.click(screen.getByRole('checkbox', { name: '选择当前页' }));
    fireEvent.click(screen.getByRole('button', { name: '删除所选（20）' }));
    fireEvent.click(screen.getByRole('button', { name: '确认批量删除' }));
    apiMocks.batchDeleteIsLoading = true;
    view.rerender(<EvidenceLibraryView onOpen={vi.fn()} />);

    expect(screen.getByRole('searchbox', { name: '筛选仿真记录', hidden: true })).toBeDisabled();
    expect(screen.getByRole('button', { name: '扫描', hidden: true })).toBeDisabled();
    expect(screen.getByRole('button', { name: '筛选仿真结果', hidden: true })).toBeDisabled();
    expect(screen.getByRole('button', { name: '按仿真时间升序', hidden: true })).toBeDisabled();
    expect(screen.getByRole('checkbox', { name: '选择当前页', hidden: true })).toBeDisabled();
    expect(screen.getByRole('combobox', { name: '每页记录数', hidden: true })).toBeDisabled();
    expect(screen.getByRole('button', { name: '上一页', hidden: true })).toBeDisabled();
    expect(screen.getByRole('button', { name: '下一页', hidden: true })).toBeDisabled();

    apiMocks.sessions = [failedSession, allSessions[0]];
    apiMocks.batchDeleteIsLoading = false;
    await act(async () => resolveBatch({
      requested: 20,
      deleted: 19,
      failed: 1,
      results: [
        ...successfulSessions.map((session) => ({
          evidence_id: session.evidence_id,
          deleted_path: session.deletion_target,
          filesystem_deleted: true,
          status: 'deleted',
        })),
        {
          evidence_id: failedSession.evidence_id,
          status: 'failed',
          error: 'unsafe <script>alert(1)</script>',
        },
      ],
    }));
    view.rerender(<EvidenceLibraryView onOpen={vi.fn()} />);

    const resultDialog = screen.getByRole('dialog', { name: '批量删除仿真记录' });
    expect(resultDialog).toHaveTextContent('已删除 19');
    expect(resultDialog).toHaveTextContent('失败 1');
    expect(resultDialog).toHaveTextContent(failedSession.evidence_id);
    expect(resultDialog).toHaveTextContent('unsafe <script>alert(1)</script>');
    expect(resultDialog.querySelector('script')).toBeNull();
    expect(screen.getByText('已选择 1 条', { selector: 'span' })).toBeInTheDocument();
    successfulSessions.forEach((session) => {
      expect(screen.queryByRole('button', { name: `删除 ${session.session_id}`, hidden: true })).not.toBeInTheDocument();
    });

    fireEvent.click(within(resultDialog).getByRole('button', { name: '重试失败项（1）' }));
    await waitFor(() => expect(apiMocks.batchDeleteSessions).toHaveBeenNthCalledWith(2, {
      evidence_ids: [failedSession.evidence_id],
    }));
    await waitFor(() => expect(screen.queryByRole('dialog', { name: '批量删除仿真记录' })).not.toBeInTheDocument());
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

  it('serializes every evidence-list query parameter exactly', async () => {
    let requestedUrl = '';
    const fetchMock = vi.fn(async (request: Request) => {
      requestedUrl = request.url;
      return jsonResponse({
        sessions: [],
        total: 0,
        filtered_total: 0,
        page: 3,
        page_size: 50,
        total_pages: 1,
        facets: { result: [], scenarioCount: [], mode: [], scenario: [], source: [], worktree: [] },
      });
    });
    const query: EvidenceLibrarySessionsQuery = {
      page: 3,
      page_size: 50,
      search: 'rule 15',
      sort_key: 'scenarioCount',
      sort_direction: 'asc',
      result: 'failed',
      scenario_count: 8,
      mode: 'full',
      scenario: 'colreg-rule15-cs',
      source: 'front',
      worktree: 'tree-a',
    };
    const { silApi, store } = await createApiStore(fetchMock);

    try {
      await store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(query)).unwrap();

      const params = Object.fromEntries(new URL(requestedUrl).searchParams.entries());
      expect(params).toEqual({
        page: '3',
        page_size: '50',
        search: 'rule 15',
        sort_key: 'scenarioCount',
        sort_direction: 'asc',
        result: 'failed',
        scenario_count: '8',
        mode: 'full',
        scenario: 'colreg-rule15-cs',
        source: 'front',
        worktree: 'tree-a',
      });
    } finally {
      vi.unstubAllGlobals();
    }
  });

  it('patches a successful single delete in a non-default page cache and preserves refetch error', async () => {
    const query: EvidenceLibrarySessionsQuery = {
      page: 2,
      page_size: 50,
      search: 'rule15',
      sort_key: 'scenario',
      sort_direction: 'asc',
      result: 'failed',
      scenario_count: 1,
      mode: 'full',
      scenario: 'colreg-rule15-cs',
      source: 'front',
      worktree: 'tree-a',
    };
    let getCount = 0;
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        if (getCount === 1) return jsonResponse({ sessions: [primarySession, secondarySession] });
        return jsonResponse({ detail: 'refetch failed' }, 500);
      }
      return jsonResponse({
        evidence_id: primarySession.evidence_id,
        deleted_path: primarySession.deletion_target,
        filesystem_deleted: true,
      });
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(query));

    try {
      await subscription.unwrap();
      await store.dispatch(
        silApi.endpoints.deleteEvidenceLibrarySession.initiate(primarySession.evidence_id),
      ).unwrap();
      await waitFor(() => expect(getCount).toBe(2));
      await waitFor(() => {
        const cached = silApi.endpoints.getEvidenceLibrarySessions.select(query)(store.getState());
        expect(cached.isError).toBe(true);
        expect(cached.data?.sessions.map((session) => session.evidence_id)).toEqual([
          secondarySession.evidence_id,
        ]);
      });
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });

  it('leaves a non-default page cache unchanged when single delete fails', async () => {
    const query: EvidenceLibrarySessionsQuery = {
      ...defaultSessionsQuery,
      page: 2,
      search: 'non-default',
    };
    let getCount = 0;
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        return jsonResponse({ sessions: [primarySession, secondarySession] });
      }
      return jsonResponse({ detail: 'delete failed' }, 500);
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(query));

    try {
      await subscription.unwrap();
      await expect(store.dispatch(
        silApi.endpoints.deleteEvidenceLibrarySession.initiate(primarySession.evidence_id),
      ).unwrap()).rejects.toBeDefined();
      expect(getCount).toBe(1);
      expect(silApi.endpoints.getEvidenceLibrarySessions.select(query)(store.getState()).data?.sessions
        .map((session) => session.evidence_id)).toEqual([
          primarySession.evidence_id,
          secondarySession.evidence_id,
        ]);
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });

  it('patches only successful batch deletions in a non-default page cache', async () => {
    const query: EvidenceLibrarySessionsQuery = {
      ...defaultSessionsQuery,
      page: 4,
      search: 'batch-page',
      sort_key: 'source',
    };
    let getCount = 0;
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        if (getCount === 1) return jsonResponse({ sessions: [primarySession, secondarySession] });
        return jsonResponse({ detail: 'refetch failed' }, 500);
      }
      return jsonResponse({
        requested: 2,
        deleted: 1,
        failed: 1,
        results: [
          {
            evidence_id: primarySession.evidence_id,
            deleted_path: primarySession.deletion_target,
            filesystem_deleted: true,
            status: 'deleted',
          },
          { evidence_id: secondarySession.evidence_id, status: 'failed', error: 'not deletable' },
        ],
      });
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(query));

    try {
      await subscription.unwrap();
      await store.dispatch(silApi.endpoints.batchDeleteEvidenceLibrarySessions.initiate({
        evidence_ids: [primarySession.evidence_id, secondarySession.evidence_id],
      })).unwrap();
      await waitFor(() => {
        const cached = silApi.endpoints.getEvidenceLibrarySessions.select(query)(store.getState());
        expect(cached.isError).toBe(true);
        expect(cached.data?.sessions.map((session) => session.evidence_id)).toEqual([
          secondarySession.evidence_id,
        ]);
      });
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });

  it('refetches once after every fulfilled scan, including zero-change and error results', async () => {
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
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(defaultSessionsQuery));

    try {
      await subscription.unwrap();
      await expect(store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap())
        .rejects.toBeDefined();
      await new Promise((resolve) => window.setTimeout(resolve, 0));
      expect(getCount).toBe(1);

      scanResponse = jsonResponse({ ingested: 0, pruned: 0, errors: [] });
      await store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap();
      await waitFor(() => expect(getCount).toBe(2));

      scanResponse = jsonResponse({
        ingested: 1,
        pruned: 1,
        errors: [{ path: '/runs/broken', error: 'partial failure' }],
      });
      await store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap();
      await waitFor(() => expect(getCount).toBe(3));
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
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(defaultSessionsQuery));

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

  it('batch delete mutation posts exact IDs and removes only successfully deleted sessions', async () => {
    let getCount = 0;
    let postedBody: unknown;
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        return jsonResponse({
          sessions: getCount === 1 ? [primarySession, secondarySession] : [secondarySession],
        });
      }
      postedBody = await request.json();
      return jsonResponse({
        requested: 2,
        deleted: 1,
        failed: 1,
        results: [
          {
            evidence_id: primarySession.evidence_id,
            deleted_path: primarySession.deletion_target,
            filesystem_deleted: true,
            status: 'deleted',
          },
          {
            evidence_id: secondarySession.evidence_id,
            status: 'failed',
            error: 'session is not deletable',
          },
        ],
      });
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(defaultSessionsQuery));

    try {
      await subscription.unwrap();
      const result = await store.dispatch(
        silApi.endpoints.batchDeleteEvidenceLibrarySessions.initiate({
          evidence_ids: [primarySession.evidence_id, secondarySession.evidence_id],
        }),
      ).unwrap();

      expect(postedBody).toEqual({
        evidence_ids: [primarySession.evidence_id, secondarySession.evidence_id],
      });
      expect(result.deleted).toBe(1);
      await waitFor(() => expect(getCount).toBe(2));
      expect(silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState()).data?.sessions
        .map((session) => session.evidence_id)).toEqual([secondarySession.evidence_id]);
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });

  it('batch delete mutation refreshes authoritative cache when the response is rejected', async () => {
    let getCount = 0;
    const fetchMock = vi.fn(async (request: Request) => {
      if (request.method === 'GET') {
        getCount += 1;
        return jsonResponse({
          sessions: getCount === 1 ? [primarySession, secondarySession] : [secondarySession],
        });
      }
      return jsonResponse({ detail: 'batch delete rejected' }, 500);
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(defaultSessionsQuery));

    try {
      await subscription.unwrap();
      await expect(store.dispatch(
        silApi.endpoints.batchDeleteEvidenceLibrarySessions.initiate({
          evidence_ids: [primarySession.evidence_id, secondarySession.evidence_id],
        }),
      ).unwrap()).rejects.toBeDefined();
      await waitFor(() => {
        expect(getCount).toBe(2);
        expect(silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState()).data?.sessions
          .map((session) => session.evidence_id)).toEqual([
            secondarySession.evidence_id,
          ]);
      });
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });

  it('batch delete mutation aborts a pending list response so stale data cannot restore deleted sessions', async () => {
    const raceSession = {
      ...primarySession,
      evidence_id: 'mounted-race-batch-delete',
      session_id: '20260713_120000_mounted_race_batch_delete',
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
      if (request.method === 'POST' && new URL(request.url).pathname.endsWith('/rescan')) {
        events.push('SCAN 200');
        return jsonResponse({ ingested: 1, pruned: 0, errors: [] });
      }
      events.push('BATCH DELETE 200');
      queueMicrotask(() => {
        events.push('GET-2 200');
        resolveStaleGet(jsonResponse({ sessions: [raceSession, secondarySession] }));
      });
      return jsonResponse({
        requested: 2,
        deleted: 1,
        failed: 1,
        results: [
          {
            evidence_id: raceSession.evidence_id,
            deleted_path: raceSession.deletion_target,
            filesystem_deleted: true,
            status: 'deleted',
          },
          {
            evidence_id: secondarySession.evidence_id,
            status: 'failed',
            error: 'session is not deletable',
          },
        ],
      });
    });
    const { silApi, store } = await createApiStore(fetchMock);
    const MountedSessions = () => {
      const { data: lastFulfilledData, currentData } = silApi.useGetEvidenceLibrarySessionsQuery(defaultSessionsQuery);
      const data = currentData ?? lastFulfilledData;
      return data?.sessions.map((session) => (
        <button key={session.evidence_id} type="button" aria-label={`mounted batch delete ${session.evidence_id}`}>
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
      await screen.findByRole('button', { name: `mounted batch delete ${raceSession.evidence_id}` });
      await act(async () => {
        await store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap();
      });
      await waitFor(() => expect(events).toEqual(['GET-1 200', 'SCAN 200', 'GET-2 pending']));

      await act(async () => {
        await store.dispatch(
          silApi.endpoints.batchDeleteEvidenceLibrarySessions.initiate({
            evidence_ids: [raceSession.evidence_id, secondarySession.evidence_id],
          }),
        ).unwrap();
      });
      await waitFor(() => expect(staleGetSignal?.aborted).toBe(true));
      await waitFor(() => expect(events).toEqual([
        'GET-1 200',
        'SCAN 200',
        'GET-2 pending',
        'BATCH DELETE 200',
        'GET-2 200',
        'GET-3 500',
      ]));
      await waitFor(() => {
        const query = silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState());
        expect(query.isError).toBe(true);
        expect(query.data?.sessions.map((session) => session.evidence_id)).toEqual([
          secondarySession.evidence_id,
        ]);
      });
    } finally {
      view.unmount();
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
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(defaultSessionsQuery));

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
        const query = silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState());
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
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(defaultSessionsQuery));

    try {
      await subscription.unwrap();
      await store.dispatch(
        silApi.endpoints.deleteEvidenceLibrarySession.initiate(primarySession.evidence_id),
      ).unwrap();
      await waitFor(() => expect(getCount).toBe(2));

      const pendingQuery = silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState());
      expect(pendingQuery.data?.sessions.map((session) => session.evidence_id)).toEqual([
        secondarySession.evidence_id,
      ]);

      resolveRefetch(jsonResponse({ detail: 'stale list refresh failed' }, 500));
      await waitFor(() => {
        const query = silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState());
        expect(query.isError).toBe(true);
      });

      const query = silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState());
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
      const { data: lastFulfilledData, currentData } = silApi.useGetEvidenceLibrarySessionsQuery(defaultSessionsQuery);
      const data = currentData ?? lastFulfilledData;
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
      expect(silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState()).data?.sessions)
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
        const query = silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState());
        expect(query.isError).toBe(true);
      });

      const query = silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState());
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
    const subscription = store.dispatch(silApi.endpoints.getEvidenceLibrarySessions.initiate(defaultSessionsQuery));

    try {
      await subscription.unwrap();
      await store.dispatch(
        silApi.endpoints.deleteEvidenceLibrarySession.initiate(rebuiltSession.evidence_id),
      ).unwrap();
      await waitFor(() => expect(getCount).toBe(2));
      expect(silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState()).data?.sessions
        .map((session) => session.evidence_id)).toEqual([secondarySession.evidence_id]);

      await store.dispatch(silApi.endpoints.rescanEvidenceLibrary.initiate({ force: false })).unwrap();
      await waitFor(() => expect(getCount).toBe(3));
      await waitFor(() => {
        expect(silApi.endpoints.getEvidenceLibrarySessions.select(defaultSessionsQuery)(store.getState()).data?.sessions
          .map((session) => session.evidence_id)).toEqual([
          rebuiltSession.evidence_id,
          secondarySession.evidence_id,
        ]);
      });
    } finally {
      subscription.unsubscribe();
      vi.unstubAllGlobals();
    }
  });
});
