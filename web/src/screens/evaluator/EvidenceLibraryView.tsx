import { useCallback, useEffect, useMemo, useRef, useState, type KeyboardEvent } from 'react';
import { flushSync } from 'react-dom';
import {
  LucideAlertTriangle,
  LucideArrowDown,
  LucideArrowUp,
  LucideImage,
  LucidePlay,
  LucideScanSearch,
  LucideSearch,
  LucideTrash2,
} from 'lucide-react';
import {
  useDeleteEvidenceLibrarySessionMutation,
  useGetEvidenceLibrarySessionsQuery,
  useRescanEvidenceLibraryMutation,
  type EvidenceLibraryScanResult,
  type EvidenceLibrarySession,
} from '../../api/silApi';

interface EvidenceLibraryViewProps {
  onOpen: (evidenceId: string) => void;
}

type SortKey = 'time' | 'result' | 'scenarioCount' | 'mode' | 'scenario' | 'source' | 'worktree';
type SortDirection = 'asc' | 'desc';
type RefreshIntervalSeconds = 600 | 3600 | 86400;

interface SessionRow {
  raw: EvidenceLibrarySession;
  time: string;
  timeValue: number;
  result: string;
  scenarioCount: number;
  mode: string;
  scenario: string;
  source: string;
  worktree: string;
}

const displayRunTime = (value?: string | null) => {
  if (!value) return '-';
  return value
    .replace('T', ' ')
    .replace(/\.\d+(?=Z|[+-]\d\d:\d\d$)/, '')
    .replace(/([+-]\d\d:\d\d|Z)$/, '');
};

const displayName = (session: EvidenceLibrarySession) => {
  const withoutTime = session.session_id.replace(/^\d{8}_\d{6}_?/, '');
  return withoutTime || session.session_id;
};

const scenarioCount = (session: EvidenceLibrarySession) =>
  session.scenario_ids?.length || session.scenario_count || 0;

const displayMode = (session: EvidenceLibrarySession) => {
  const text = `${session.session_id} ${session.suite}`.toLowerCase();
  if (text.includes('debug') || text.includes('dbg') || text.includes('trace') || text.includes('ctx')) return '调试验证';
  if (text.includes('cohort')) return '同类验证';
  if (text.includes('full') || text.includes('clean8') || text.includes('clean12')) return '完整验证';
  if (text.includes('fast') || session.suite === 'single') return '避碰验证';
  return '调试验证';
};

const displayScenario = (session: EvidenceLibrarySession) => {
  const scenarios = session.scenario_ids ?? [];
  if (scenarios.length === 0) return '-';
  if (scenarios.length <= 2) return scenarios.join(', ');
  return `${scenarios[0]} +${scenarios.length - 1}`;
};

const displaySource = (session: EvidenceLibrarySession) => {
  const source = session.source.toLowerCase();
  if (source === 'cli') return 'CLI';
  if (source === 'frontend' || source === 'front') return 'Front';
  return session.source || '-';
};

const displayWorktree = (session: EvidenceLibrarySession) => {
  if (displaySource(session) === 'Front') return '';
  if (session.worktree_name) return session.worktree_name;
  const match = session.session_path.match(/\/\.worktrees\/([^/]+)\//);
  if (match) return match[1];
  return session.branch || '-';
};

const canOpen = (session: EvidenceLibrarySession) =>
  session.ingest_status === 'ok' && Boolean(session.valid_data) && scenarioCount(session) > 0;

const hasOverview = (session: EvidenceLibrarySession) =>
  Boolean(session.overview_png || session.overview_pngs?.length);

const displayResult = (session: EvidenceLibrarySession) => {
  const count = scenarioCount(session);
  const passed = session.passed_scenarios ?? 0;
  const failed = session.failed_scenarios ?? 0;
  if (count > 1) return `${passed}/${count} 通过`;
  if (passed > 0) return '通过';
  if (failed > 0) return '不通过';
  return '-';
};

const toRow = (session: EvidenceLibrarySession): SessionRow => ({
  raw: session,
  time: displayRunTime(session.created_at),
  timeValue: session.created_at ? Date.parse(session.created_at) || 0 : 0,
  result: displayResult(session),
  scenarioCount: scenarioCount(session),
  mode: displayMode(session),
  scenario: displayScenario(session),
  source: displaySource(session),
  worktree: displayWorktree(session),
});

const compareRows = (a: SessionRow, b: SessionRow, key: SortKey) => {
  if (key === 'time') return a.timeValue - b.timeValue;
  if (key === 'scenarioCount') return a.scenarioCount - b.scenarioCount;
  return String(a[key]).localeCompare(String(b[key]), 'zh-Hans-CN', { numeric: true });
};

const uniqueValues = (rows: SessionRow[], key: SortKey) =>
  Array.from(new Set(rows.map((row) => String(row[key])).filter(Boolean))).sort((a, b) =>
    a.localeCompare(b, 'zh-Hans-CN', { numeric: true }),
  );

const matchesSearch = (row: SessionRow, searchText: string) => {
  const query = searchText.trim().toLocaleLowerCase();
  if (!query) return true;
  const values = [
    row.raw.evidence_id,
    row.raw.session_id,
    row.scenario,
    ...(row.raw.scenario_ids ?? []),
    row.source,
    row.raw.source,
    row.raw.suite,
    row.mode,
    row.worktree,
    row.raw.worktree_name,
    row.raw.branch,
    row.result,
  ];
  return values.some((value) => String(value ?? '').toLocaleLowerCase().includes(query));
};

const cellStyle = {
  height: 42,
  padding: '0 8px',
  boxSizing: 'border-box',
  whiteSpace: 'nowrap',
  overflow: 'hidden',
  textOverflow: 'ellipsis',
  textAlign: 'center',
} as const;

const headerButtonStyle = {
  border: '1px solid var(--line-2)',
  background: 'rgba(7, 12, 19, 0.6)',
  color: 'var(--txt-2)',
  width: 22,
  height: 22,
  lineHeight: '18px',
  padding: 0,
  cursor: 'pointer',
} as const;

const sortDirectionButtonStyle = {
  ...headerButtonStyle,
  width: 18,
  height: 10,
  lineHeight: '10px',
  display: 'inline-flex',
  alignItems: 'center',
  justifyContent: 'center',
} as const;

const filterStyle = {
  width: '100%',
  border: '1px solid var(--line-2)',
  background: 'var(--bg-0)',
  color: 'var(--txt-1)',
  fontFamily: 'var(--f-mono)',
  fontSize: 10,
  height: 22,
} as const;

const refreshControlStyle = {
  ...filterStyle,
  height: 26,
  borderRadius: 4,
  boxSizing: 'border-box',
} as const;

const actionButtonStyle = {
  height: 28,
  padding: '0 9px',
  borderRadius: 4,
  display: 'inline-flex',
  alignItems: 'center',
  justifyContent: 'center',
  gap: 5,
  whiteSpace: 'nowrap',
  fontFamily: 'var(--f-mono)',
  fontSize: 11,
} as const;

const formatCountdown = (seconds: number) => {
  const clamped = Math.max(0, seconds);
  const hours = Math.floor(clamped / 3600);
  const minutes = Math.floor((clamped % 3600) / 60);
  const secs = clamped % 60;
  if (hours > 0) return `${hours}:${String(minutes).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
  return `${minutes}:${String(secs).padStart(2, '0')}`;
};

export function EvidenceLibraryView({ onOpen }: EvidenceLibraryViewProps) {
  const { data, isLoading } = useGetEvidenceLibrarySessionsQuery();
  const [rescan, rescanState] = useRescanEvidenceLibraryMutation();
  const [deleteSession, deleteState] = useDeleteEvidenceLibrarySessionMutation();
  const sessions = data?.sessions ?? [];
  const [sort, setSort] = useState<{ key: SortKey; direction: SortDirection }>({ key: 'time', direction: 'desc' });
  const [pageSize, setPageSize] = useState<20 | 50>(20);
  const [page, setPage] = useState(0);
  const [filters, setFilters] = useState<Partial<Record<SortKey, string>>>({});
  const [searchText, setSearchText] = useState('');
  const [searchFocused, setSearchFocused] = useState(false);
  const [autoRefreshSeconds, setAutoRefreshSeconds] = useState<RefreshIntervalSeconds>(86400);
  const [countdownSeconds, setCountdownSeconds] = useState(86400);
  const [scanFailed, setScanFailed] = useState(false);
  const [scanResult, setScanResult] = useState<EvidenceLibraryScanResult | null>(null);
  const [overviewSession, setOverviewSession] = useState<EvidenceLibrarySession | null>(null);
  const [overviewIndex, setOverviewIndex] = useState(0);
  const [overviewScale, setOverviewScale] = useState(1);
  const [overviewOffset, setOverviewOffset] = useState({ x: 0, y: 0 });
  const [overviewDrag, setOverviewDrag] = useState<{ startX: number; startY: number; originX: number; originY: number } | null>(null);
  const [pendingDelete, setPendingDelete] = useState<EvidenceLibrarySession | null>(null);
  const [deleteFailed, setDeleteFailed] = useState(false);
  const [hoveredRowId, setHoveredRowId] = useState<string | null>(null);
  const deleteDialogRef = useRef<HTMLDivElement | null>(null);
  const deleteCancelButtonRef = useRef<HTMLButtonElement | null>(null);
  const deleteTriggerRef = useRef<HTMLButtonElement | null>(null);
  const searchInputRef = useRef<HTMLInputElement | null>(null);
  const [focusSearchAfterDeleteId, setFocusSearchAfterDeleteId] = useState<string | null>(null);
  const backgroundRef = useRef<HTMLElement | null>(null);
  const overviewDialogRef = useRef<HTMLDivElement | null>(null);
  const automaticScanAttemptedRef = useRef(false);

  const rows = useMemo(() => sessions.map(toRow), [sessions]);
  const filteredRows = useMemo(() => {
    const activeFilters = Object.entries(filters).filter(([, value]) => value);
    return rows.filter((row) =>
      matchesSearch(row, searchText)
      && activeFilters.every(([key, value]) => String(row[key as SortKey]) === value),
    );
  }, [filters, rows, searchText]);
  const sortedRows = useMemo(() => {
    return [...filteredRows].sort((a, b) => {
      const result = compareRows(a, b, sort.key);
      return sort.direction === 'asc' ? result : -result;
    });
  }, [filteredRows, sort]);
  const totalPages = Math.max(1, Math.ceil(sortedRows.length / pageSize));
  const safePage = Math.min(page, totalPages - 1);
  const visibleRows = sortedRows.slice(safePage * pageSize, safePage * pageSize + pageSize);
  const deleteActionsDisabled = deleteState.isLoading;
  const overviewPngs = overviewSession
    ? overviewSession.overview_pngs?.length
      ? overviewSession.overview_pngs
      : overviewSession.overview_png
        ? [overviewSession.overview_png]
        : []
    : [];
  const currentOverview = overviewPngs[Math.min(overviewIndex, Math.max(overviewPngs.length - 1, 0))];

  const resetOverviewView = () => {
    setOverviewScale(1);
    setOverviewOffset({ x: 0, y: 0 });
    setOverviewDrag(null);
  };

  const closeOverview = () => {
    setOverviewSession(null);
    setOverviewIndex(0);
    resetOverviewView();
  };

  const openDeleteDialog = (session: EvidenceLibrarySession, trigger: HTMLButtonElement) => {
    if (deleteState.isLoading || !session.deletion_allowed || !session.deletion_target) return;
    setDeleteFailed(false);
    deleteTriggerRef.current = trigger;
    setPendingDelete(session);
  };

  const closeDeleteDialog = (restoreTrigger = true) => {
    const trigger = deleteTriggerRef.current;
    setDeleteFailed(false);
    flushSync(() => setPendingDelete(null));
    if (restoreTrigger) {
      trigger?.focus();
      deleteTriggerRef.current = null;
    }
  };

  const cancelDelete = () => {
    if (deleteState.isLoading) return;
    closeDeleteDialog();
  };

  const handleDelete = async () => {
    if (!pendingDelete) return;
    const target = pendingDelete;
    try {
      await deleteSession(target.evidence_id).unwrap();
      if (overviewSession?.evidence_id === target.evidence_id) closeOverview();
      setFocusSearchAfterDeleteId(target.evidence_id);
      closeDeleteDialog(false);
    } catch {
      setDeleteFailed(true);
    }
  };

  const changeOverviewImage = (delta: number) => {
    if (overviewPngs.length <= 1) return;
    setOverviewIndex((current) => (current + delta + overviewPngs.length) % overviewPngs.length);
    resetOverviewView();
  };

  const zoomOverview = (delta: number) => {
    setOverviewScale((current) => Math.max(0.5, Math.min(4, Number((current + delta).toFixed(2)))));
  };

  const handleRescan = useCallback(async () => {
    try {
      const result = await rescan({ force: false }).unwrap();
      setScanFailed(false);
      setScanResult(result);
      automaticScanAttemptedRef.current = false;
      setCountdownSeconds(autoRefreshSeconds);
    } catch {
      setScanFailed(true);
    }
  }, [autoRefreshSeconds, rescan]);

  const handleDeleteDialogKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    if (event.key === 'Escape') {
      event.preventDefault();
      cancelDelete();
      return;
    }
    if (event.key !== 'Tab') return;

    const focusable = Array.from(
      deleteDialogRef.current?.querySelectorAll<HTMLElement>(
        'button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])',
      ) ?? [],
    );
    if (focusable.length === 0) {
      event.preventDefault();
      return;
    }

    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
    } else if (!deleteDialogRef.current?.contains(document.activeElement)) {
      event.preventDefault();
      first.focus();
    }
  };

  useEffect(() => {
    automaticScanAttemptedRef.current = false;
    setCountdownSeconds(autoRefreshSeconds);
  }, [autoRefreshSeconds]);

  useEffect(() => {
    if (pendingDelete) deleteCancelButtonRef.current?.focus();
  }, [pendingDelete]);

  useEffect(() => {
    for (const element of [backgroundRef.current, overviewDialogRef.current]) {
      if (!element) continue;
      if (pendingDelete) element.setAttribute('inert', '');
      else element.removeAttribute('inert');
    }
  }, [pendingDelete]);

  useEffect(() => {
    if (!focusSearchAfterDeleteId || pendingDelete) return;
    const searchInput = searchInputRef.current;
    searchInput?.focus();
    if (searchInput && document.activeElement === searchInput) {
      setFocusSearchAfterDeleteId(null);
      deleteTriggerRef.current = null;
    }
  }, [focusSearchAfterDeleteId, pendingDelete]);

  useEffect(() => {
    const timer = window.setInterval(() => {
      setCountdownSeconds((current) => {
        if (current <= 1) {
          if (!automaticScanAttemptedRef.current) {
            automaticScanAttemptedRef.current = true;
            void handleRescan();
          }
          return 0;
        }
        return current - 1;
      });
    }, 1000);
    return () => window.clearInterval(timer);
  }, [autoRefreshSeconds, handleRescan]);

  const setSortDirection = (key: SortKey, direction: SortDirection) => {
    setSort({ key, direction });
    setPage(0);
  };

  const setFilter = (key: SortKey, value: string) => {
    setFilters((current) => ({ ...current, [key]: value }));
    setPage(0);
  };

  const enumFilter = (key: SortKey) => (
    <select
      value={filters[key] ?? ''}
      onChange={(event) => setFilter(key, event.target.value)}
      style={{ ...filterStyle, width: 76 }}
      aria-label={`${key} filter`}
    >
      <option value="">全部</option>
      {uniqueValues(rows, key).map((value) => (
        <option key={value} value={value}>{value}</option>
      ))}
    </select>
  );

  const sortDirectionControls = (label: string, key: SortKey) => (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 1 }}>
      {(['asc', 'desc'] as const).map((direction) => {
        const active = sort.key === key && sort.direction === direction;
        const directionLabel = direction === 'asc' ? '升序' : '降序';
        const Icon = direction === 'asc' ? LucideArrowUp : LucideArrowDown;
        return (
          <button
            key={direction}
            type="button"
            onClick={() => setSortDirection(key, direction)}
            style={{
              ...sortDirectionButtonStyle,
              color: active ? 'var(--c-phos)' : 'var(--txt-3)',
              background: active ? 'rgba(69, 211, 207, 0.1)' : sortDirectionButtonStyle.background,
            }}
            aria-label={`按${label}${directionLabel}`}
            aria-pressed={active}
            title={`按${label}${directionLabel}`}
          >
            <Icon size={10} aria-hidden="true" />
          </button>
        );
      })}
    </div>
  );

  const columnHeader = (
    label: string,
    key: SortKey,
    width: number,
    filter?: 'enum',
  ) => (
    <th align="center" style={{ width, padding: '8px 8px 7px', verticalAlign: 'middle', textAlign: 'center' }}>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6, minHeight: 24 }}>
        <span style={{ flex: 1, textAlign: 'center', whiteSpace: 'nowrap' }}>{label}</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
          {filter === 'enum' && enumFilter(key)}
          {sortDirectionControls(label, key)}
        </div>
      </div>
    </th>
  );

  return (
    <div style={{
      height: '100%',
      display: 'flex',
      background: 'var(--bg-0)',
      color: 'var(--txt-1)',
    }}>
      <main
        ref={backgroundRef}
        aria-hidden={pendingDelete ? true : undefined}
        style={{ flex: 1, padding: 12, overflow: 'hidden', display: 'flex', flexDirection: 'column', minWidth: 0 }}
      >
        {isLoading ? (
          <div>Loading evidence</div>
        ) : (
          <>
            <div style={{
              display: 'flex',
              alignItems: 'center',
              flexWrap: 'wrap',
              gap: 10,
              padding: '0 0 10px',
              fontFamily: 'var(--f-mono)',
              minHeight: 36,
            }}>
              <div style={{ display: 'flex', alignItems: 'baseline', gap: 14, flex: '0 1 auto' }}>
                <h1 style={{
                  fontFamily: 'var(--f-disp)',
                  fontSize: 15,
                  fontWeight: 800,
                  letterSpacing: 0,
                  color: 'var(--txt-1)',
                  margin: 0,
                  whiteSpace: 'nowrap',
                }}>
                  仿真数据库
                </h1>
                <div style={{ display: 'flex', gap: 12, color: 'var(--txt-3)', fontSize: 11, whiteSpace: 'nowrap' }}>
                  <span>记录数: {sessions.length}</span>
                  <span>显示: {sortedRows.length}</span>
                </div>
              </div>
              <label style={{
                position: 'relative',
                flex: '1 1 260px',
                maxWidth: 420,
                minWidth: 220,
              }}>
                <LucideSearch
                  size={14}
                  aria-hidden="true"
                  style={{ position: 'absolute', left: 9, top: 7, color: 'var(--txt-3)', pointerEvents: 'none' }}
                />
                <input
                  ref={searchInputRef}
                  type="search"
                  aria-label="筛选仿真记录"
                  value={searchText}
                  onChange={(event) => {
                    setSearchText(event.target.value);
                    setPage(0);
                  }}
                  onFocus={() => setSearchFocused(true)}
                  onBlur={() => setSearchFocused(false)}
                  placeholder="筛选会话、场景、来源、工作树"
                  style={{
                    ...refreshControlStyle,
                    width: '100%',
                    padding: '0 10px 0 30px',
                    outline: searchFocused ? '2px solid var(--c-phos)' : '2px solid transparent',
                    outlineOffset: 2,
                  }}
                />
              </label>
              <div style={{
                marginLeft: 'auto',
                display: 'flex',
                alignItems: 'center',
                gap: 8,
                color: 'var(--txt-3)',
                fontSize: 12,
              }}>
                <button
                  type="button"
                  aria-label={rescanState.isLoading ? '扫描中' : '扫描'}
                  title="扫描证据库"
                  onClick={handleRescan}
                  disabled={rescanState.isLoading}
                  style={{
                    border: '1px solid var(--c-phos)',
                    color: 'var(--c-phos)',
                    background: 'rgba(69, 211, 207, 0.06)',
                    height: 26,
                    minWidth: 60,
                    padding: '0 14px',
                    borderRadius: 4,
                    whiteSpace: 'nowrap',
                    cursor: rescanState.isLoading ? 'wait' : 'pointer',
                    display: 'inline-flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                    gap: 6,
                  }}
                >
                  <LucideScanSearch size={14} aria-hidden="true" />
                  {rescanState.isLoading ? '扫描中' : '扫描'}
                </button>
                <select
                  value={autoRefreshSeconds}
                  onChange={(event) => setAutoRefreshSeconds(Number(event.target.value) as RefreshIntervalSeconds)}
                  style={{ ...refreshControlStyle, width: 86 }}
                  aria-label="自动刷新间隔"
                >
                  <option value={600}>10min</option>
                  <option value={3600}>60min</option>
                  <option value={86400}>24h</option>
                </select>
                <span style={{
                  ...refreshControlStyle,
                  width: 58,
                  display: 'inline-flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  padding: '0 6px',
                }} aria-label="距离下次扫描">
                  {formatCountdown(countdownSeconds)}
                </span>
              </div>
            </div>
            {scanFailed && (
              <div
                role="alert"
                style={{
                  margin: '0 0 10px',
                  padding: '8px 10px',
                  border: '1px solid rgba(255, 91, 112, 0.5)',
                  borderRadius: 4,
                  background: 'rgba(255, 91, 112, 0.08)',
                  color: 'var(--c-danger)',
                  fontFamily: 'var(--f-mono)',
                  fontSize: 11,
                }}
              >
                扫描失败，请保留当前结果后重试。
              </div>
            )}
            {scanResult && (
              <div
                role={scanResult.errors.length > 0 ? 'alert' : 'status'}
                style={{
                  margin: '0 0 10px',
                  padding: '8px 10px',
                  border: scanResult.errors.length > 0
                    ? '1px solid rgba(255, 91, 112, 0.5)'
                    : '1px solid rgba(69, 211, 207, 0.42)',
                  borderRadius: 4,
                  background: scanResult.errors.length > 0
                    ? 'rgba(255, 91, 112, 0.08)'
                    : 'rgba(69, 211, 207, 0.06)',
                  color: scanResult.errors.length > 0 ? 'var(--c-danger)' : 'var(--txt-2)',
                  fontFamily: 'var(--f-mono)',
                  fontSize: 11,
                }}
              >
                <strong>
                  {scanResult.errors.length > 0 ? '扫描部分完成' : '扫描完成'}：写入 {scanResult.ingested}，清理 {scanResult.pruned}，错误 {scanResult.errors.length}。
                </strong>
                {scanResult.errors.length > 0 && (
                  <ul style={{ margin: '6px 0 0', paddingLeft: 18 }}>
                    {scanResult.errors.map((error, index) => (
                      <li key={`${error.path}-${index}`}>
                        <code>{error.path}</code>: {error.error}
                      </li>
                    ))}
                  </ul>
                )}
              </div>
            )}
            <div style={{
              flex: 1,
              minHeight: 0,
              overflow: 'auto',
              border: '1px solid var(--line-2)',
              background: 'linear-gradient(180deg, rgba(18, 29, 44, 0.72), rgba(7, 12, 19, 0.9))',
            }}>
              <table style={{ width: '100%', minWidth: 1400, tableLayout: 'fixed', borderCollapse: 'collapse', fontFamily: 'var(--f-mono)', fontSize: 12 }}>
                <thead style={{ position: 'sticky', top: 0, zIndex: 2, background: 'rgba(13, 23, 35, 0.96)' }}>
                  <tr style={{ color: 'var(--txt-3)', borderBottom: '1px solid var(--line-2)' }}>
                    <th align="center" style={{ width: 60, padding: '8px 8px 7px', verticalAlign: 'middle', textAlign: 'center' }}>序号</th>
                    {columnHeader('仿真时间', 'time', 150)}
                    {columnHeader('仿真结果', 'result', 120)}
                    {columnHeader('场景数量', 'scenarioCount', 150, 'enum')}
                    {columnHeader('模式', 'mode', 125, 'enum')}
                    {columnHeader('仿真场景', 'scenario', 180)}
                    {columnHeader('来源', 'source', 125, 'enum')}
                    {columnHeader('工作树', 'worktree', 170)}
                    <th align="center" style={{ width: 240, padding: '8px 8px 7px', verticalAlign: 'middle', textAlign: 'center' }}>操作</th>
                  </tr>
                </thead>
                <tbody>
                  {visibleRows.map((row, index) => (
                    <tr
                      key={row.raw.evidence_id}
                      style={{
                        borderTop: '1px solid rgba(78, 108, 139, 0.24)',
                        background: hoveredRowId === row.raw.evidence_id
                          ? 'rgba(69, 211, 207, 0.09)'
                          : index % 2 === 0 ? 'rgba(255, 255, 255, 0.018)' : 'transparent',
                        transition: 'background 120ms ease',
                      }}
                      onMouseEnter={() => setHoveredRowId(row.raw.evidence_id)}
                      onMouseLeave={() => setHoveredRowId(null)}
                    >
                      <td style={cellStyle}>{safePage * pageSize + index + 1}</td>
                      <td style={cellStyle} title={row.raw.created_at ?? ''}>{row.time}</td>
                      <td style={cellStyle}>
                        <span style={{
                          display: 'inline-flex',
                          alignItems: 'center',
                          justifyContent: 'center',
                          minWidth: 48,
                          height: 22,
                          padding: '0 7px',
                          borderRadius: 3,
                          border: row.result === '不通过'
                            ? '1px solid rgba(255, 91, 112, 0.55)'
                            : row.result === '通过' || row.result.includes('/')
                              ? '1px solid rgba(69, 211, 207, 0.48)'
                              : '1px solid var(--line-2)',
                          background: row.result === '不通过'
                            ? 'rgba(255, 91, 112, 0.1)'
                            : row.result === '通过' || row.result.includes('/')
                              ? 'rgba(69, 211, 207, 0.08)'
                              : 'rgba(255, 255, 255, 0.025)',
                          color: row.result === '不通过'
                            ? 'var(--c-danger)'
                            : row.result === '通过' || row.result.includes('/')
                              ? 'var(--c-stbd)'
                              : 'var(--txt-3)',
                          fontWeight: 700,
                        }}>
                          {row.result}
                        </span>
                      </td>
                      <td style={cellStyle}>{row.scenarioCount}</td>
                      <td style={cellStyle}>{row.mode}</td>
                      <td style={cellStyle} title={(row.raw.scenario_ids ?? []).join(', ')}>{row.scenario}</td>
                      <td style={cellStyle}>{row.source}</td>
                      <td style={cellStyle} title={row.worktree}>{row.worktree}</td>
                      <td style={cellStyle}>
                        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6, height: '100%' }}>
                          <button
                            type="button"
                            disabled={!hasOverview(row.raw)}
                            title="打开仿真概述"
                            onClick={() => {
                              if (hasOverview(row.raw)) {
                                setOverviewSession(row.raw);
                                setOverviewIndex(0);
                                resetOverviewView();
                              }
                            }}
                            style={{
                              ...actionButtonStyle,
                              border: '1px solid var(--line-2)',
                              background: hasOverview(row.raw) ? 'rgba(69, 211, 207, 0.07)' : 'transparent',
                              color: hasOverview(row.raw) ? 'var(--txt-1)' : 'var(--txt-3)',
                              cursor: hasOverview(row.raw) ? 'pointer' : 'not-allowed',
                            }}
                          >
                            <LucideImage size={13} aria-hidden="true" />
                            概述
                          </button>
                          <button
                            type="button"
                            disabled={!canOpen(row.raw)}
                            title="打开轨迹回放"
                            onClick={() => {
                              if (canOpen(row.raw)) onOpen(row.raw.evidence_id);
                            }}
                            style={{
                              ...actionButtonStyle,
                              border: '1px solid var(--line-2)',
                              background: canOpen(row.raw) ? 'rgba(69, 211, 207, 0.07)' : 'transparent',
                              color: canOpen(row.raw) ? 'var(--txt-1)' : 'var(--txt-3)',
                              cursor: canOpen(row.raw) ? 'pointer' : 'not-allowed',
                            }}
                          >
                            <LucidePlay size={13} aria-hidden="true" />
                            回放
                          </button>
                          <button
                            type="button"
                            aria-label={`删除 ${row.raw.session_id}`}
                            title={row.raw.deletion_allowed && row.raw.deletion_target
                              ? `删除 ${row.raw.session_id}`
                              : row.raw.deletion_error || '删除目标不安全'}
                            disabled={deleteActionsDisabled || !row.raw.deletion_allowed || !row.raw.deletion_target}
                            onClick={(event) => openDeleteDialog(row.raw, event.currentTarget)}
                            style={{
                              ...actionButtonStyle,
                              border: '1px solid rgba(255, 91, 112, 0.52)',
                              background: 'rgba(255, 91, 112, 0.08)',
                              color: 'var(--c-danger)',
                              cursor: deleteActionsDisabled
                                ? 'wait'
                                : row.raw.deletion_allowed && row.raw.deletion_target ? 'pointer' : 'not-allowed',
                            }}
                          >
                            <LucideTrash2 size={13} aria-hidden="true" />
                            删除
                          </button>
                        </div>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <div style={{
              display: 'flex',
              justifyContent: 'flex-end',
              alignItems: 'center',
              gap: 10,
              paddingTop: 10,
              fontFamily: 'var(--f-mono)',
              fontSize: 12,
              color: 'var(--txt-2)',
            }}>
              <span>每页</span>
              <select
                aria-label="每页记录数"
                value={pageSize}
                onChange={(event) => {
                  setPageSize(Number(event.target.value) as 20 | 50);
                  setPage(0);
                }}
                style={{ ...filterStyle, width: 72, marginTop: 0 }}
              >
                <option value={20}>20</option>
                <option value={50}>50</option>
              </select>
              <span>{safePage + 1} / {totalPages}</span>
              <button type="button" aria-label="上一页" disabled={safePage <= 0} onClick={() => setPage(safePage - 1)} style={headerButtonStyle}>‹</button>
              <button type="button" aria-label="下一页" disabled={safePage >= totalPages - 1} onClick={() => setPage(safePage + 1)} style={headerButtonStyle}>›</button>
            </div>
          </>
        )}
      </main>
      {pendingDelete && (
        <div
          ref={deleteDialogRef}
          role="dialog"
          aria-label="删除仿真记录"
          aria-modal="true"
          onKeyDown={handleDeleteDialogKeyDown}
          style={{
            position: 'fixed',
            inset: 0,
            zIndex: 70,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            padding: 20,
            background: 'rgba(0, 0, 0, 0.76)',
          }}
          onClick={cancelDelete}
        >
          <div
            style={{
              width: 'min(560px, 100%)',
              border: '1px solid rgba(255, 91, 112, 0.5)',
              borderRadius: 6,
              background: 'var(--bg-1)',
              boxShadow: '0 18px 50px rgba(0, 0, 0, 0.45)',
              padding: 18,
              fontFamily: 'var(--f-mono)',
            }}
            onClick={(event) => event.stopPropagation()}
          >
            <div style={{ display: 'flex', alignItems: 'center', gap: 9, color: 'var(--c-danger)' }}>
              <LucideAlertTriangle size={18} aria-hidden="true" />
              <h2 style={{ margin: 0, fontSize: 15, letterSpacing: 0 }}>删除仿真记录</h2>
            </div>
            <p style={{ margin: '16px 0 7px', color: 'var(--txt-2)', fontSize: 12, lineHeight: 1.6 }}>
              将永久删除会话 <strong style={{ color: 'var(--txt-1)' }}>{pendingDelete.session_id}</strong>。
            </p>
            <p style={{ margin: '0 0 8px', color: 'var(--txt-3)', fontSize: 11 }}>服务器确认删除目标</p>
            <code style={{
              display: 'block',
              padding: '9px 10px',
              border: '1px solid var(--line-2)',
              borderRadius: 4,
              background: 'var(--bg-0)',
              color: 'var(--txt-2)',
              fontSize: 11,
              lineHeight: 1.5,
              overflowWrap: 'anywhere',
              whiteSpace: 'normal',
            }}>
              {pendingDelete.deletion_target}
            </code>
            {deleteFailed && (
              <div
                role="alert"
                style={{
                  marginTop: 12,
                  padding: '9px 10px',
                  border: '1px solid rgba(255, 91, 112, 0.5)',
                  borderRadius: 4,
                  background: 'rgba(255, 91, 112, 0.08)',
                  color: 'var(--c-danger)',
                  fontSize: 11,
                }}
              >
                删除失败，请保留记录后重试。
              </div>
            )}
            <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8, marginTop: 18 }}>
              <button
                ref={deleteCancelButtonRef}
                type="button"
                onClick={cancelDelete}
                disabled={deleteState.isLoading}
                style={{
                  ...actionButtonStyle,
                  minWidth: 72,
                  border: '1px solid var(--line-2)',
                  background: 'rgba(255, 255, 255, 0.025)',
                  color: 'var(--txt-2)',
                  cursor: deleteState.isLoading ? 'not-allowed' : 'pointer',
                }}
              >
                取消
              </button>
              <button
                type="button"
                onClick={() => void handleDelete()}
                disabled={deleteState.isLoading}
                style={{
                  ...actionButtonStyle,
                  minWidth: 96,
                  border: '1px solid var(--c-danger)',
                  background: 'rgba(255, 91, 112, 0.13)',
                  color: 'var(--c-danger)',
                  cursor: deleteState.isLoading ? 'wait' : 'pointer',
                }}
              >
                <LucideTrash2 size={13} aria-hidden="true" />
                {deleteState.isLoading ? '删除中' : '确认删除'}
              </button>
            </div>
          </div>
        </div>
      )}
      {overviewSession && (
        <div
          ref={overviewDialogRef}
          role="dialog"
          aria-label="仿真概述"
          aria-hidden={pendingDelete ? true : undefined}
          style={{
            position: 'fixed',
            inset: 0,
            zIndex: 50,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            background: 'rgba(0, 0, 0, 0.72)',
          }}
          onClick={closeOverview}
        >
          <div
            className="glass-panel"
            style={{
              width: '96vw',
              height: '88vh',
              border: '1px solid var(--line-2)',
              background: 'var(--bg-1)',
              display: 'flex',
              flexDirection: 'column',
            }}
            onClick={(event) => event.stopPropagation()}
          >
            <div style={{
              height: 42,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'space-between',
              padding: '0 14px',
              borderBottom: '1px solid var(--line-2)',
              fontFamily: 'var(--f-mono)',
              fontSize: 12,
            }}>
              <span>
                仿真概述 · {displayName(overviewSession)}
                {currentOverview ? ` · ${currentOverview.scenario_id} · ${overviewIndex + 1}/${overviewPngs.length}` : ''}
              </span>
              <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                {overviewPngs.length > 1 && (
                  <>
                    <button
                      type="button"
                      onClick={() => changeOverviewImage(-1)}
                      style={{ ...headerButtonStyle, width: 28, height: 24 }}
                      title="上一张"
                    >
                      ‹
                    </button>
                    <button
                      type="button"
                      onClick={() => changeOverviewImage(1)}
                      style={{ ...headerButtonStyle, width: 28, height: 24 }}
                      title="下一张"
                    >
                      ›
                    </button>
                  </>
                )}
                <button
                  type="button"
                  onClick={() => zoomOverview(-0.25)}
                  style={{ ...headerButtonStyle, width: 30, height: 24 }}
                  title="缩小"
                >
                  -
                </button>
                <span style={{ color: 'var(--txt-2)', minWidth: 42, textAlign: 'center' }}>
                  {Math.round(overviewScale * 100)}%
                </span>
                <button
                  type="button"
                  onClick={() => zoomOverview(0.25)}
                  style={{ ...headerButtonStyle, width: 30, height: 24 }}
                  title="放大"
                >
                  +
                </button>
                <button
                  type="button"
                  onClick={resetOverviewView}
                  title="重置"
                  style={{
                    border: '1px solid var(--line-2)',
                    background: 'rgba(69, 211, 207, 0.08)',
                    color: 'var(--txt-1)',
                    height: 24,
                    padding: '0 10px',
                    cursor: 'pointer',
                  }}
                >
                  重置
                </button>
                <button
                  type="button"
                  onClick={closeOverview}
                  style={{
                    border: '1px solid var(--line-2)',
                    background: 'rgba(69, 211, 207, 0.08)',
                    color: 'var(--txt-1)',
                    height: 24,
                    padding: '0 12px',
                    cursor: 'pointer',
                  }}
                >
                  关闭
                </button>
              </div>
            </div>
            <div
              style={{
                flex: 1,
                minHeight: 0,
                padding: 8,
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                overflow: 'hidden',
                cursor: overviewDrag ? 'grabbing' : 'grab',
              }}
              onWheel={(event) => {
                event.preventDefault();
                zoomOverview(event.deltaY > 0 ? -0.15 : 0.15);
              }}
              onMouseDown={(event) => {
                setOverviewDrag({
                  startX: event.clientX,
                  startY: event.clientY,
                  originX: overviewOffset.x,
                  originY: overviewOffset.y,
                });
              }}
              onMouseMove={(event) => {
                if (!overviewDrag) return;
                setOverviewOffset({
                  x: overviewDrag.originX + event.clientX - overviewDrag.startX,
                  y: overviewDrag.originY + event.clientY - overviewDrag.startY,
                });
              }}
              onMouseUp={() => setOverviewDrag(null)}
              onMouseLeave={() => setOverviewDrag(null)}
            >
              {currentOverview && (
                <img
                  alt={`${currentOverview.scenario_id} 仿真概述`}
                  draggable={false}
                  src={`/api/v1/evidence-library/sessions/${encodeURIComponent(overviewSession.evidence_id)}/overview-png?scenario_id=${encodeURIComponent(currentOverview.scenario_id)}`}
                  style={{
                    width: '100%',
                    height: '100%',
                    objectFit: 'contain',
                    border: '1px solid var(--line-1)',
                    transform: `translate(${overviewOffset.x}px, ${overviewOffset.y}px) scale(${overviewScale})`,
                    transformOrigin: 'center center',
                    userSelect: 'none',
                  }}
                />
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
