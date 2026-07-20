import { useCallback, useEffect, useMemo, useRef, useState, type KeyboardEvent as ReactKeyboardEvent } from 'react';
import { flushSync } from 'react-dom';
import {
  LucideAlertTriangle,
  LucideArrowDown,
  LucideArrowUp,
  LucideImage,
  LucideListFilter,
  LucidePlay,
  LucideScanSearch,
  LucideSearch,
  LucideTrash2,
} from 'lucide-react';
import {
  useBatchDeleteEvidenceLibrarySessionsMutation,
  useDeleteEvidenceLibrarySessionMutation,
  useGetEvidenceLibrarySessionsQuery,
  useLazyGetEvidenceLibraryRescanStatusQuery,
  useRescanEvidenceLibraryMutation,
  type EvidenceLibraryBatchDeleteResult,
  type EvidenceLibraryDeleteResult,
  type EvidenceLibraryScanResult,
  type EvidenceLibrarySession,
} from '../../api/silApi';

interface EvidenceLibraryViewProps {
  onOpen: (evidenceId: string) => void;
}

type SortKey = 'time' | 'result' | 'scenarioCount' | 'mode' | 'scenario' | 'source' | 'worktree';
type SortDirection = 'asc' | 'desc';
type OutcomeCategory = 'passed' | 'failed' | 'unknown';

interface SessionRow {
  raw: EvidenceLibrarySession;
  time: string;
  timeValue: number;
  result: string;
  resultDisplay: string;
  outcome: OutcomeCategory;
  scenarioCount: number;
  mode: string;
  scenario: string;
  source: string;
  worktree: string;
}

interface SelectedSessionSnapshot {
  readonly evidenceId: string;
  readonly outcome: OutcomeCategory;
  readonly worktreeName: string | null;
}

type EvidenceLibraryRecoveryScanResult = EvidenceLibraryScanResult & {
  cleanup_pending?: EvidenceLibraryDeleteResult[];
};

const PENDING_CLEANUP_STORAGE_PREFIX = 'mass-l3:evidence-library:pending-cleanup:v1';

const cleanupStorageKeyForConfig = (value: unknown) => {
  if (typeof value !== 'object' || value === null) return null;
  const config = value as Record<string, unknown>;
  if (
    typeof config.config_home !== 'string'
    || typeof config.database_path !== 'string'
    || !Array.isArray(config.roots)
  ) return null;
  const roots = config.roots.map((value) => {
    if (typeof value !== 'object' || value === null) return null;
    const root = value as Record<string, unknown>;
    if (typeof root.root_id !== 'string' || typeof root.path_glob !== 'string') return null;
    return {
      allow_retention_mutation: root.allow_retention_mutation === true,
      enabled: root.enabled !== false,
      follow_symlinks: root.follow_symlinks === true,
      path_glob: root.path_glob,
      root_id: root.root_id,
      trusted: root.trusted === true,
    };
  });
  if (roots.some((root) => root === null)) return null;
  roots.sort((left, right) => JSON.stringify(left).localeCompare(JSON.stringify(right)));
  const identity = JSON.stringify([config.config_home, config.database_path, roots]);
  return `${PENDING_CLEANUP_STORAGE_PREFIX}:${encodeURIComponent(identity)}`;
};

const isSafePersistedString = (value: unknown): value is string => (
  typeof value === 'string' && value.length > 0 && !value.includes('\0')
);

const isPersistedCleanupResult = (value: unknown): value is EvidenceLibraryDeleteResult => {
  if (typeof value !== 'object' || value === null) return false;
  const result = value as Record<string, unknown>;
  return isSafePersistedString(result.evidence_id)
    && isSafePersistedString(result.deleted_path)
    && typeof result.filesystem_deleted === 'boolean'
    && result.filesystem_cleanup === 'pending'
    && isSafePersistedString(result.cleanup_path)
    && (result.cleanup_error === undefined || typeof result.cleanup_error === 'string')
    && (result.cleanup_metadata_path === undefined || isSafePersistedString(result.cleanup_metadata_path))
    && (
      result.cleanup_paths === undefined
      || (
        Array.isArray(result.cleanup_paths)
        && result.cleanup_paths.every(isSafePersistedString)
      )
    );
};

const loadPendingCleanup = (storageKey: string) => {
  const pending = new Map<string, EvidenceLibraryDeleteResult>();
  if (typeof window === 'undefined') return pending;
  try {
    const parsed: unknown = JSON.parse(window.localStorage.getItem(storageKey) ?? '[]');
    if (!Array.isArray(parsed)) return pending;
    for (const value of parsed) {
      if (isPersistedCleanupResult(value)) pending.set(value.cleanup_path!, value);
    }
  } catch {
    return pending;
  }
  return pending;
};

const additionalCleanupPaths = (result: EvidenceLibraryDeleteResult) => {
  const primaryPaths = new Set([result.cleanup_path, result.cleanup_metadata_path]);
  return (result.cleanup_paths ?? []).filter((path) => path && !primaryPaths.has(path));
};

const displayRunTime = (value?: string | null) => {
  if (!value) return '-';
  return value
    .replace('T', ' ')
    .replace(/\.\d+(?=Z|[+-]\d\d:\d\d$|$)/, '')
    .replace(/([+-]\d\d:\d\d|Z)$/, '');
};

const displayName = (session: EvidenceLibrarySession) => {
  const withoutTime = session.session_id.replace(/^\d{8}_\d{6}_?/, '');
  return withoutTime || session.session_id;
};

const scenarioCount = (session: EvidenceLibrarySession) =>
  session.scenario_ids != null ? session.scenario_ids.length : session.scenario_count ?? 0;

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

const outcomeCategory = (session: EvidenceLibrarySession): OutcomeCategory => {
  const count = scenarioCount(session);
  const passed = session.passed_scenarios ?? 0;
  const failed = session.failed_scenarios ?? 0;
  if (failed > 0) return 'failed';
  if (count > 0 && passed === count) return 'passed';
  return 'unknown';
};

const outcomeLabel = (outcome: OutcomeCategory) => {
  if (outcome === 'passed') return '通过';
  if (outcome === 'failed') return '不通过';
  return '-';
};

const snapshotSession = (session: EvidenceLibrarySession): SelectedSessionSnapshot => ({
  evidenceId: session.evidence_id,
  outcome: outcomeCategory(session),
  worktreeName: session.worktree_name || null,
});

const toRow = (session: EvidenceLibrarySession): SessionRow => {
  const outcome = outcomeCategory(session);
  return {
    raw: session,
    time: displayRunTime(session.created_at),
    timeValue: session.created_at ? Date.parse(session.created_at) || 0 : 0,
    result: outcomeLabel(outcome),
    resultDisplay: displayResult(session),
    outcome,
    scenarioCount: scenarioCount(session),
    mode: displayMode(session),
    scenario: displayScenario(session),
    source: displaySource(session),
    worktree: displayWorktree(session),
  };
};

const compareRows = (a: SessionRow, b: SessionRow, key: SortKey) => {
  if (key === 'time') return a.timeValue - b.timeValue;
  if (key === 'scenarioCount') return a.scenarioCount - b.scenarioCount;
  return String(a[key]).localeCompare(String(b[key]), 'zh-Hans-CN', { numeric: true });
};

const uniqueValues = (rows: SessionRow[], key: SortKey) => {
  const values = Array.from(new Set(rows.map((row) => String(row[key])).filter(Boolean)));
  if (key === 'scenarioCount') return values.sort((a, b) => Number(a) - Number(b));
  return values.sort((a, b) => a.localeCompare(b, 'zh-Hans-CN', { numeric: true }));
};

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
    row.resultDisplay,
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

const filterTriggerStyle = {
  ...headerButtonStyle,
  width: 24,
  height: 24,
  lineHeight: '22px',
  display: 'inline-flex',
  alignItems: 'center',
  justifyContent: 'center',
  borderRadius: 4,
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

export function EvidenceLibraryView({ onOpen }: EvidenceLibraryViewProps) {
  const { data, isLoading, refetch: refetchSessions } = useGetEvidenceLibrarySessionsQuery();
  const [rescan, rescanState] = useRescanEvidenceLibraryMutation();
  const [getRescanStatus] = useLazyGetEvidenceLibraryRescanStatusQuery();
  const [deleteSession, deleteState] = useDeleteEvidenceLibrarySessionMutation();
  const [batchDeleteSessions, batchDeleteState] = useBatchDeleteEvidenceLibrarySessionsMutation();
  const sessions = data?.sessions ?? [];
  const [sort, setSort] = useState<{ key: SortKey; direction: SortDirection }>({ key: 'time', direction: 'desc' });
  const [pageSize, setPageSize] = useState<20 | 50>(20);
  const [page, setPage] = useState(0);
  const [filters, setFilters] = useState<Partial<Record<SortKey, string>>>({});
  const [openFilterKey, setOpenFilterKey] = useState<SortKey | null>(null);
  const [searchText, setSearchText] = useState('');
  const [searchFocused, setSearchFocused] = useState(false);
  const [selectedSessionSnapshots, setSelectedSessionSnapshots] = useState<Map<string, SelectedSessionSnapshot>>(
    () => new Map(),
  );
  const [scanFailed, setScanFailed] = useState(false);
  const [scanResult, setScanResult] = useState<EvidenceLibraryScanResult | null>(null);
  const [overviewSession, setOverviewSession] = useState<EvidenceLibrarySession | null>(null);
  const [overviewIndex, setOverviewIndex] = useState(0);
  const [overviewScale, setOverviewScale] = useState(1);
  const [overviewOffset, setOverviewOffset] = useState({ x: 0, y: 0 });
  const [overviewDrag, setOverviewDrag] = useState<{ startX: number; startY: number; originX: number; originY: number } | null>(null);
  const [pendingDelete, setPendingDelete] = useState<EvidenceLibrarySession | null>(null);
  const [deleteFailed, setDeleteFailed] = useState(false);
  const [deletePersistenceFailed, setDeletePersistenceFailed] = useState(false);
  const [pendingBatchDelete, setPendingBatchDelete] = useState<SelectedSessionSnapshot[] | null>(null);
  const [batchDeleteResult, setBatchDeleteResult] = useState<EvidenceLibraryBatchDeleteResult | null>(null);
  const [batchDeletePersistenceFailed, setBatchDeletePersistenceFailed] = useState(false);
  const [batchDeleteNeedsRescan, setBatchDeleteNeedsRescan] = useState(false);
  const [scanReconciliationPending, setScanReconciliationPending] = useState(false);
  const [cleanupStorageKey, setCleanupStorageKey] = useState<string | null>(null);
  const [cleanupStorageReady, setCleanupStorageReady] = useState(false);
  const [cleanupStorageUnavailable, setCleanupStorageUnavailable] = useState(false);
  const [cleanupStorageOperationPending, setCleanupStorageOperationPending] = useState(false);
  const [pendingCleanupByPath, setPendingCleanupByPath] = useState<Map<string, EvidenceLibraryDeleteResult>>(
    () => new Map(),
  );
  const [hoveredRowId, setHoveredRowId] = useState<string | null>(null);
  const deleteDialogRef = useRef<HTMLDivElement | null>(null);
  const batchDeleteDialogRef = useRef<HTMLDivElement | null>(null);
  const filterMenuRef = useRef<HTMLDivElement | null>(null);
  const filterTriggerRefs = useRef<Partial<Record<SortKey, HTMLButtonElement | null>>>({});
  const deleteCancelButtonRef = useRef<HTMLButtonElement | null>(null);
  const deleteTriggerRef = useRef<HTMLButtonElement | null>(null);
  const batchDeleteCancelButtonRef = useRef<HTMLButtonElement | null>(null);
  const batchDeleteTriggerRef = useRef<HTMLButtonElement | null>(null);
  const searchInputRef = useRef<HTMLInputElement | null>(null);
  const [focusSearchAfterDeleteId, setFocusSearchAfterDeleteId] = useState<string | null>(null);
  const [focusSearchAfterBatchDelete, setFocusSearchAfterBatchDelete] = useState(false);
  const backgroundRef = useRef<HTMLElement | null>(null);
  const overviewDialogRef = useRef<HTMLDivElement | null>(null);
  const selectPageRef = useRef<HTMLInputElement | null>(null);
  const pendingCleanupByPathRef = useRef<Map<string, EvidenceLibraryDeleteResult>>(new Map());
  const acknowledgedCleanupPathsRef = useRef<Set<string>>(new Set());
  const cleanupStorageReadyRef = useRef(false);
  const cleanupStorageActiveRef = useRef(true);
  const cleanupStorageAttemptRef = useRef<Promise<boolean> | null>(null);
  const scanMountedRef = useRef(true);
  const scanPollTimerRef = useRef<number | null>(null);

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
  const selectableRows = sortedRows.filter((row) => row.raw.deletion_allowed && row.raw.deletion_target);
  const visibleSelectableRows = visibleRows.filter(
    (row) => row.raw.deletion_allowed && row.raw.deletion_target,
  );
  const visibleSelectableIds = visibleSelectableRows.map((row) => row.raw.evidence_id);
  const selectedEvidenceIds = new Set(selectedSessionSnapshots.keys());
  const selectedSessions = Array.from(selectedSessionSnapshots.values());
  const selectedOnPage = visibleSelectableIds.filter((id) => selectedEvidenceIds.has(id)).length;
  const allVisibleSelected = visibleSelectableIds.length > 0 && selectedOnPage === visibleSelectableIds.length;
  const visibleSelectionMixed = selectedOnPage > 0 && !allVisibleSelected;
  const deleteActionsDisabled = deleteState.isLoading
    || batchDeleteState.isLoading
    || cleanupStorageOperationPending;
  const scanJobActive = scanResult?.state === 'queued' || scanResult?.state === 'running';
  const scanInProgress = rescanState.isLoading || scanReconciliationPending || scanJobActive;
  const scanProgressLabel = scanJobActive
    ? (scanResult.total > 0 ? `扫描 ${scanResult.processed}/${scanResult.total}` : '扫描中')
    : '扫描';
  const destructiveActionsDisabled = deleteActionsDisabled
    || scanInProgress
    || batchDeleteNeedsRescan
    || cleanupStorageUnavailable;
  const selectionDisabled = destructiveActionsDisabled;
  const allFilteredSelected = selectableRows.length > 0
    && selectableRows.every((row) => selectedEvidenceIds.has(row.raw.evidence_id));
  const batchDeleteSummary = useMemo(() => {
    const summary = { passed: 0, failed: 0, unknown: 0, worktrees: 0 };
    if (!pendingBatchDelete) return summary;

    for (const session of pendingBatchDelete) {
      if (session.outcome === 'failed') summary.failed += 1;
      else if (session.outcome === 'passed') summary.passed += 1;
      else summary.unknown += 1;
    }
    summary.worktrees = new Set(
      pendingBatchDelete.map((session) => session.worktreeName).filter((name): name is string => Boolean(name)),
    ).size;
    return summary;
  }, [pendingBatchDelete]);
  const batchDeleteFailures = batchDeleteResult?.results.filter((item) => item.status === 'failed') ?? [];
  const batchDeleteCleanupPending = Array.from(pendingCleanupByPath.values());
  const overviewPngs = overviewSession
    ? overviewSession.overview_pngs?.length
      ? overviewSession.overview_pngs
      : overviewSession.overview_png
        ? [overviewSession.overview_png]
        : []
    : [];
  const currentOverview = overviewPngs[Math.min(overviewIndex, Math.max(overviewPngs.length - 1, 0))];

  const recordPendingCleanup = useCallback((results: EvidenceLibraryDeleteResult[]) => {
    const next = new Map(pendingCleanupByPathRef.current);
    for (const result of results) {
      if (result.filesystem_cleanup === 'pending' && result.cleanup_path) {
        acknowledgedCleanupPathsRef.current.delete(result.cleanup_path);
        next.set(result.cleanup_path, result);
      }
    }
    pendingCleanupByPathRef.current = next;
    setPendingCleanupByPath(next);
  }, []);

  const acknowledgePendingCleanup = useCallback((cleanupPath: string) => {
    acknowledgedCleanupPathsRef.current.add(cleanupPath);
    const next = new Map(pendingCleanupByPathRef.current);
    next.delete(cleanupPath);
    pendingCleanupByPathRef.current = next;
    setPendingCleanupByPath(next);
  }, []);

  const markCleanupStorageUnavailable = useCallback(() => {
    cleanupStorageReadyRef.current = false;
    setCleanupStorageReady(false);
    setCleanupStorageUnavailable(true);
  }, []);

  const initializeCleanupStorage = useCallback(() => {
    if (cleanupStorageReadyRef.current) return Promise.resolve(true);
    if (cleanupStorageAttemptRef.current) return cleanupStorageAttemptRef.current;

    const attempt = Promise.resolve()
      .then(async () => {
        const response = await fetch('/api/v1/evidence-library/config', {
          headers: { Accept: 'application/json' },
        });
        if (!response.ok) throw new Error('Evidence cleanup identity request failed');
        const storageKey = cleanupStorageKeyForConfig(await response.json());
        if (!storageKey) throw new Error('Evidence cleanup identity is invalid');
        if (!cleanupStorageActiveRef.current) return false;
        const stored = loadPendingCleanup(storageKey);
        acknowledgedCleanupPathsRef.current.forEach((path) => stored.delete(path));
        pendingCleanupByPathRef.current.forEach((result, path) => {
          if (!acknowledgedCleanupPathsRef.current.has(path)) stored.set(path, result);
        });
        window.localStorage.setItem(
          storageKey,
          JSON.stringify(Array.from(stored.values())),
        );
        pendingCleanupByPathRef.current = stored;
        setPendingCleanupByPath(stored);
        setCleanupStorageKey(storageKey);
        cleanupStorageReadyRef.current = true;
        setCleanupStorageReady(true);
        setCleanupStorageUnavailable(false);
        return true;
      })
      .catch(() => {
        if (cleanupStorageActiveRef.current) markCleanupStorageUnavailable();
        return false;
      });
    const trackedAttempt = attempt.finally(() => {
      if (cleanupStorageAttemptRef.current === trackedAttempt) {
        cleanupStorageAttemptRef.current = null;
      }
    });
    cleanupStorageAttemptRef.current = trackedAttempt;
    return trackedAttempt;
  }, [markCleanupStorageUnavailable]);

  useEffect(() => {
    cleanupStorageActiveRef.current = true;
    void initializeCleanupStorage();
    return () => {
      cleanupStorageActiveRef.current = false;
    };
  }, [initializeCleanupStorage]);

  useEffect(() => {
    if (!cleanupStorageKey || !cleanupStorageReady) return;
    try {
      window.localStorage.setItem(
        cleanupStorageKey,
        JSON.stringify(Array.from(pendingCleanupByPath.values())),
      );
    } catch {
      markCleanupStorageUnavailable();
    }
  }, [cleanupStorageKey, cleanupStorageReady, markCleanupStorageUnavailable, pendingCleanupByPath]);

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
    if (destructiveActionsDisabled || !session.deletion_allowed || !session.deletion_target) return;
    setDeleteFailed(false);
    setDeletePersistenceFailed(false);
    deleteTriggerRef.current = trigger;
    setPendingDelete(session);
  };

  const closeDeleteDialog = (restoreTrigger = true) => {
    const trigger = deleteTriggerRef.current;
    setDeleteFailed(false);
    setDeletePersistenceFailed(false);
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
    if (!pendingDelete || destructiveActionsDisabled) return;
    const target = pendingDelete;
    setCleanupStorageOperationPending(true);
    try {
      if (!await initializeCleanupStorage()) {
        setDeletePersistenceFailed(true);
        return;
      }
      setDeletePersistenceFailed(false);
      const result = await deleteSession(target.evidence_id).unwrap();
      recordPendingCleanup([result]);
      if (overviewSession?.evidence_id === target.evidence_id) closeOverview();
      setFocusSearchAfterDeleteId(target.evidence_id);
      closeDeleteDialog(false);
    } catch {
      setDeleteFailed(true);
    } finally {
      setCleanupStorageOperationPending(false);
    }
  };

  const openBatchDeleteDialog = (trigger: HTMLButtonElement) => {
    if (destructiveActionsDisabled || selectedSessions.length === 0) return;
    batchDeleteTriggerRef.current = trigger;
    setBatchDeleteResult(null);
    setBatchDeletePersistenceFailed(false);
    setPendingBatchDelete([...selectedSessions]);
  };

  const closeBatchDeleteDialog = () => {
    if (batchDeleteState.isLoading) return;
    const trigger = batchDeleteTriggerRef.current;
    setBatchDeleteResult(null);
    setBatchDeletePersistenceFailed(false);
    flushSync(() => setPendingBatchDelete(null));
    trigger?.focus();
    batchDeleteTriggerRef.current = null;
  };

  const handleBatchDelete = async () => {
    if (!pendingBatchDelete || pendingBatchDelete.length === 0 || destructiveActionsDisabled) return;
    setCleanupStorageOperationPending(true);
    try {
      if (!await initializeCleanupStorage()) {
        setBatchDeletePersistenceFailed(true);
        return;
      }
      setBatchDeletePersistenceFailed(false);
      const result = await batchDeleteSessions({
        evidence_ids: pendingBatchDelete.map((session) => session.evidenceId),
      }).unwrap();
      recordPendingCleanup(result.results.flatMap((item) => (
        item.status === 'deleted' ? [item] : []
      )));
      const failedIds = new Set(
        result.results.filter((item) => item.status === 'failed').map((item) => item.evidence_id),
      );
      const failedSnapshots = pendingBatchDelete.filter((session) => failedIds.has(session.evidenceId));
      setSelectedSessionSnapshots(new Map(
        failedSnapshots.map((session) => [session.evidenceId, session]),
      ));
      setBatchDeleteResult(result);
      const cleanupPending = result.results.some(
        (item) => item.status === 'deleted' && item.filesystem_cleanup === 'pending',
      );
      if (result.failed === 0 && !cleanupPending) {
        setPendingBatchDelete(null);
        setBatchDeleteResult(null);
        batchDeleteTriggerRef.current = null;
        setFocusSearchAfterBatchDelete(true);
      } else {
        setPendingBatchDelete(failedSnapshots);
      }
    } catch {
      setBatchDeleteNeedsRescan(true);
    } finally {
      setCleanupStorageOperationPending(false);
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

  const clearSelection = useCallback(() => {
    setSelectedSessionSnapshots(new Map());
  }, []);

  const waitForScanPoll = useCallback(() => new Promise<void>((resolve) => {
    scanPollTimerRef.current = window.setTimeout(() => {
      scanPollTimerRef.current = null;
      resolve();
    }, 100);
  }), []);

  const handleRescan = useCallback(async () => {
    clearSelection();
    setScanReconciliationPending(true);
    try {
      let result = await rescan({ force: false }).unwrap() as EvidenceLibraryRecoveryScanResult;
      setScanFailed(false);
      setScanResult(result);
      while (
        scanMountedRef.current
        && (result.state === 'queued' || result.state === 'running')
      ) {
        await waitForScanPoll();
        if (!scanMountedRef.current) return;
        result = await getRescanStatus().unwrap() as EvidenceLibraryRecoveryScanResult;
        setScanResult(result);
      }
      if (!scanMountedRef.current) return;
      recordPendingCleanup(result.cleanup_pending ?? []);
      if (result.state === 'failed') {
        setScanFailed(true);
        return;
      }
      await refetchSessions().unwrap();
      if (result.errors.length === 0) setBatchDeleteNeedsRescan(false);
    } catch {
      setScanFailed(true);
      setScanResult((current) => current && (
        current.state === 'queued' || current.state === 'running'
      ) ? {
          ...current,
          state: 'failed',
          finished_at: new Date().toISOString(),
        } : current);
    } finally {
      if (cleanupStorageUnavailable) await initializeCleanupStorage();
      setScanReconciliationPending(false);
    }
  }, [
    cleanupStorageUnavailable,
    clearSelection,
    getRescanStatus,
    initializeCleanupStorage,
    recordPendingCleanup,
    refetchSessions,
    rescan,
    waitForScanPoll,
  ]);

  const toggleSelected = (session: EvidenceLibrarySession) => {
    if (selectionDisabled || !session.deletion_allowed || !session.deletion_target) return;
    setSelectedSessionSnapshots((current) => {
      const next = new Map(current);
      if (next.has(session.evidence_id)) next.delete(session.evidence_id);
      else next.set(session.evidence_id, snapshotSession(session));
      return next;
    });
  };

  const toggleCurrentPage = () => {
    if (selectionDisabled) return;
    setSelectedSessionSnapshots((current) => {
      const next = new Map(current);
      if (visibleSelectableIds.every((id) => next.has(id))) {
        visibleSelectableIds.forEach((id) => next.delete(id));
      } else {
        visibleSelectableRows.forEach((row) => {
          next.set(row.raw.evidence_id, snapshotSession(row.raw));
        });
      }
      return next;
    });
  };

  const selectAllFiltered = () => {
    if (selectionDisabled) return;
    setSelectedSessionSnapshots(new Map(
      selectableRows.map((row) => [row.raw.evidence_id, snapshotSession(row.raw)]),
    ));
  };

  const trapDialogFocus = (
    event: ReactKeyboardEvent<HTMLDivElement>,
    dialogRef: { current: HTMLDivElement | null },
    cancel: () => void,
  ) => {
    if (event.key === 'Escape') {
      event.preventDefault();
      cancel();
      return;
    }
    if (event.key !== 'Tab') return;

    const focusable = Array.from(
      dialogRef.current?.querySelectorAll<HTMLElement>(
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
    } else if (!dialogRef.current?.contains(document.activeElement)) {
      event.preventDefault();
      first.focus();
    }
  };

  const handleDeleteDialogKeyDown = (event: ReactKeyboardEvent<HTMLDivElement>) => {
    trapDialogFocus(event, deleteDialogRef, cancelDelete);
  };

  const handleBatchDeleteDialogKeyDown = (event: ReactKeyboardEvent<HTMLDivElement>) => {
    trapDialogFocus(event, batchDeleteDialogRef, closeBatchDeleteDialog);
  };

  useEffect(() => {
    scanMountedRef.current = true;
    return () => {
      scanMountedRef.current = false;
      if (scanPollTimerRef.current !== null) {
        window.clearTimeout(scanPollTimerRef.current);
        scanPollTimerRef.current = null;
      }
    };
  }, []);

  useEffect(() => {
    if (pendingDelete) deleteCancelButtonRef.current?.focus();
  }, [pendingDelete]);

  useEffect(() => {
    if (pendingBatchDelete) batchDeleteCancelButtonRef.current?.focus();
  }, [pendingBatchDelete]);

  useEffect(() => {
    for (const element of [backgroundRef.current, overviewDialogRef.current]) {
      if (!element) continue;
      if (pendingDelete || pendingBatchDelete) element.setAttribute('inert', '');
      else element.removeAttribute('inert');
    }
  }, [pendingBatchDelete, pendingDelete]);

  useEffect(() => {
    if (!focusSearchAfterDeleteId || pendingDelete || cleanupStorageOperationPending) return;
    const searchInput = searchInputRef.current;
    searchInput?.focus();
    if (searchInput && document.activeElement === searchInput) {
      setFocusSearchAfterDeleteId(null);
      deleteTriggerRef.current = null;
    }
  }, [cleanupStorageOperationPending, focusSearchAfterDeleteId, pendingDelete]);

  useEffect(() => {
    if (!focusSearchAfterBatchDelete || pendingBatchDelete || cleanupStorageOperationPending) return;
    searchInputRef.current?.focus();
    setFocusSearchAfterBatchDelete(false);
  }, [cleanupStorageOperationPending, focusSearchAfterBatchDelete, pendingBatchDelete]);

  useEffect(() => {
    if (selectPageRef.current) selectPageRef.current.indeterminate = visibleSelectionMixed;
  }, [visibleSelectionMixed]);

  useEffect(() => {
    if (!openFilterKey) return;
    const handlePointerDown = (event: PointerEvent) => {
      if (!(event.target instanceof Node)) return;
      const trigger = filterTriggerRefs.current[openFilterKey];
      if (filterMenuRef.current?.contains(event.target) || trigger?.contains(event.target)) return;
      setOpenFilterKey(null);
    };
    const handleKeyDown = (event: globalThis.KeyboardEvent) => {
      if (event.key !== 'Escape') return;
      event.preventDefault();
      const trigger = filterTriggerRefs.current[openFilterKey];
      flushSync(() => setOpenFilterKey(null));
      trigger?.focus();
    };
    document.addEventListener('pointerdown', handlePointerDown);
    document.addEventListener('keydown', handleKeyDown);
    return () => {
      document.removeEventListener('pointerdown', handlePointerDown);
      document.removeEventListener('keydown', handleKeyDown);
    };
  }, [openFilterKey]);

  const setSortDirection = (key: SortKey, direction: SortDirection) => {
    clearSelection();
    setSort({ key, direction });
    setPage(0);
  };

  const setFilter = (key: SortKey, value: string) => {
    clearSelection();
    setFilters((current) => ({ ...current, [key]: value }));
    setPage(0);
  };

  const popoverFilter = (key: SortKey, label: string) => {
    const open = openFilterKey === key;
    const value = filters[key] ?? '';
    return (
      <div style={{ position: 'relative', display: 'inline-flex', alignItems: 'center' }}>
        <button
          ref={(element) => {
            filterTriggerRefs.current[key] = element;
          }}
          type="button"
          disabled={deleteActionsDisabled}
          onClick={() => setOpenFilterKey((current) => current === key ? null : key)}
          style={{
            ...filterTriggerStyle,
            color: value ? 'var(--c-phos)' : filterTriggerStyle.color,
            background: value ? 'rgba(69, 211, 207, 0.1)' : filterTriggerStyle.background,
          }}
          aria-label={`筛选${label}`}
          aria-expanded={open}
          aria-haspopup="menu"
          aria-controls={`${key}-filter-menu`}
          title={`筛选${label}`}
        >
          <LucideListFilter size={13} aria-hidden="true" />
        </button>
        {value && (
          <span
            aria-label={`${label}筛选值`}
            style={{
              position: 'absolute',
              top: -5,
              right: -5,
              minWidth: 12,
              height: 12,
              padding: '0 2px',
              borderRadius: 4,
              background: 'var(--c-phos)',
              color: 'var(--bg-0)',
              fontSize: 8,
              fontWeight: 800,
              lineHeight: '12px',
              textAlign: 'center',
              pointerEvents: 'none',
            }}
          >
            {value}
          </span>
        )}
        {open && (
          <div
            ref={filterMenuRef}
            id={`${key}-filter-menu`}
            role="menu"
            aria-label={`${label}筛选选项`}
            style={{
              position: 'absolute',
              top: 'calc(100% + 6px)',
              right: 0,
              zIndex: 30,
              minWidth: 78,
              padding: 4,
              border: '1px solid var(--line-2)',
              borderRadius: 4,
              background: 'var(--bg-0)',
              boxShadow: '0 6px 16px rgba(0, 0, 0, 0.28)',
            }}
          >
            {['', ...uniqueValues(rows, key)].map((option) => (
              <button
                key={option || 'all'}
                type="button"
                role="menuitem"
                disabled={deleteActionsDisabled}
                onClick={() => {
                  setFilter(key, option);
                  const trigger = filterTriggerRefs.current[key];
                  flushSync(() => setOpenFilterKey(null));
                  trigger?.focus();
                }}
                style={{
                  display: 'block',
                  width: '100%',
                  minHeight: 24,
                  padding: '0 7px',
                  border: 0,
                  borderRadius: 3,
                  background: option === value ? 'rgba(69, 211, 207, 0.12)' : 'transparent',
                  color: option === value ? 'var(--c-phos)' : 'var(--txt-1)',
                  fontFamily: 'var(--f-mono)',
                  fontSize: 10,
                  textAlign: 'left',
                  cursor: 'pointer',
                }}
              >
                {option || '全部'}
              </button>
            ))}
          </div>
        )}
      </div>
    );
  };

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
            disabled={deleteActionsDisabled}
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
    filter?: boolean,
  ) => (
    <th align="center" style={{ width, padding: '8px 8px 7px', verticalAlign: 'middle', textAlign: 'center' }}>
      <div style={{ position: 'relative', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6, minHeight: 24 }}>
        <span style={{ flex: 1, textAlign: 'center', whiteSpace: 'nowrap' }}>{label}</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
          {filter && popoverFilter(key, label)}
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
        aria-hidden={pendingDelete || pendingBatchDelete ? true : undefined}
        style={{ flex: 1, padding: 12, overflow: 'hidden', display: 'flex', flexDirection: 'column', minWidth: 0 }}
      >
        {isLoading && !data ? (
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
                  disabled={deleteActionsDisabled}
                  value={searchText}
                  onChange={(event) => {
                    clearSelection();
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
              {selectedSessions.length > 0 && (
                <div style={{ display: 'inline-flex', alignItems: 'center', gap: 7, color: 'var(--txt-2)', fontSize: 11, whiteSpace: 'nowrap' }}>
                  <span>已选择 {selectedSessions.length} 条</span>
                  {selectableRows.length > 0 && !allFilteredSelected && (
                    <button
                      type="button"
                      onClick={selectAllFiltered}
                      disabled={selectionDisabled}
                      style={{
                        ...actionButtonStyle,
                        height: 26,
                        border: '1px solid var(--line-2)',
                        background: 'rgba(69, 211, 207, 0.06)',
                        color: 'var(--c-phos)',
                        cursor: selectionDisabled ? 'not-allowed' : 'pointer',
                      }}
                    >
                      选择全部 {selectableRows.length} 条筛选结果
                    </button>
                  )}
                  <button
                    type="button"
                    onClick={clearSelection}
                    disabled={deleteActionsDisabled || scanInProgress}
                    style={{
                      ...actionButtonStyle,
                      height: 26,
                      border: '1px solid var(--line-2)',
                      background: 'rgba(255, 255, 255, 0.025)',
                      color: 'var(--txt-2)',
                      cursor: deleteActionsDisabled || scanInProgress ? 'not-allowed' : 'pointer',
                    }}
                  >
                    取消选择
                  </button>
                  <button
                    ref={batchDeleteTriggerRef}
                    type="button"
                    title={batchDeleteNeedsRescan ? '请先扫描核对证据库' : undefined}
                    onClick={(event) => openBatchDeleteDialog(event.currentTarget)}
                    disabled={destructiveActionsDisabled}
                    style={{
                      ...actionButtonStyle,
                      height: 26,
                      border: '1px solid rgba(255, 91, 112, 0.52)',
                      background: 'rgba(255, 91, 112, 0.08)',
                      color: 'var(--c-danger)',
                      cursor: destructiveActionsDisabled ? 'not-allowed' : 'pointer',
                    }}
                  >
                    <LucideTrash2 size={13} aria-hidden="true" />
                    删除所选（{selectedSessions.length}）
                  </button>
                </div>
              )}
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
                  aria-label={scanProgressLabel}
                  title="扫描证据库"
                  onClick={handleRescan}
                  disabled={scanInProgress || deleteActionsDisabled}
                  style={{
                    border: '1px solid var(--c-phos)',
                    color: 'var(--c-phos)',
                    background: 'rgba(69, 211, 207, 0.06)',
                    height: 26,
                    minWidth: 60,
                    padding: '0 14px',
                    borderRadius: 4,
                    whiteSpace: 'nowrap',
                    cursor: scanInProgress || deleteActionsDisabled ? 'wait' : 'pointer',
                    display: 'inline-flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                    gap: 6,
                  }}
                >
                  <LucideScanSearch size={14} aria-hidden="true" />
                  {scanProgressLabel}
                </button>
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
            {batchDeleteNeedsRescan && (
              <div
                role="alert"
                style={{
                  margin: '0 0 10px',
                  padding: '8px 10px',
                  border: '1px solid rgba(255, 190, 70, 0.5)',
                  borderRadius: 4,
                  background: 'rgba(255, 190, 70, 0.08)',
                  color: 'var(--txt-2)',
                  fontFamily: 'var(--f-mono)',
                  fontSize: 11,
                }}
              >
                批量删除结果未知。请扫描核对证据库后重新选择。
              </div>
            )}
            {cleanupStorageUnavailable && (
              <div
                role="alert"
                style={{
                  margin: '0 0 10px',
                  padding: '8px 10px',
                  border: '1px solid rgba(255, 190, 70, 0.5)',
                  borderRadius: 4,
                  background: 'rgba(255, 190, 70, 0.08)',
                  color: 'var(--txt-2)',
                  fontFamily: 'var(--f-mono)',
                  fontSize: 11,
                }}
              >
                清理记录未能持久保存，删除已锁定。请扫描恢复后再执行删除。
              </div>
            )}
            {batchDeleteCleanupPending.length > 0 && (
              <div
                role="alert"
                aria-label="待处理文件清理"
                style={{
                  margin: '0 0 10px',
                  padding: '8px 10px',
                  border: '1px solid rgba(255, 190, 70, 0.5)',
                  borderRadius: 4,
                  background: 'rgba(255, 190, 70, 0.08)',
                  color: 'var(--txt-2)',
                  fontFamily: 'var(--f-mono)',
                  fontSize: 11,
                }}
              >
                <strong>文件清理待确认 {batchDeleteCleanupPending.length}</strong>
                <ul style={{ margin: '6px 0 0', paddingLeft: 18 }}>
                  {batchDeleteCleanupPending.map((item) => (
                    <li key={item.cleanup_path} style={{ marginTop: 6, overflowWrap: 'anywhere' }}>
                      <code>{item.evidence_id}</code>: {item.cleanup_error}
                      <div>暂存路径：<code>{item.cleanup_path}</code></div>
                      {item.cleanup_metadata_path && (
                        <div>恢复元数据：<code>{item.cleanup_metadata_path}</code></div>
                      )}
                      {additionalCleanupPaths(item).map((path) => (
                        <div key={path}>待清理路径：<code>{path}</code></div>
                      ))}
                      <button
                        type="button"
                        aria-label={`确认已记录 ${item.evidence_id}`}
                        onClick={() => acknowledgePendingCleanup(item.cleanup_path!)}
                        style={{ ...actionButtonStyle, marginTop: 5 }}
                      >
                        已记录
                      </button>
                    </li>
                  ))}
                </ul>
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
                  {scanJobActive
                    ? `扫描中：处理 ${scanResult.processed}/${scanResult.total}`
                    : scanResult.state === 'failed'
                      ? '扫描失败'
                      : scanResult.errors.length > 0
                        ? '扫描部分完成'
                        : '扫描完成'}
                  {!scanJobActive && <>：写入 {scanResult.ingested}，跳过 {scanResult.skipped ?? 0}，清理 {scanResult.pruned}，错误 {scanResult.errors.length}。</>}
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
                    <th align="center" style={{ width: 42, padding: '8px 0 7px', verticalAlign: 'middle', textAlign: 'center' }}>
                      <input
                        ref={selectPageRef}
                        type="checkbox"
                        aria-label="选择当前页"
                        checked={allVisibleSelected}
                        disabled={visibleSelectableIds.length === 0 || selectionDisabled}
                        onChange={toggleCurrentPage}
                      />
                    </th>
                    <th align="center" style={{ width: 60, padding: '8px 8px 7px', verticalAlign: 'middle', textAlign: 'center' }}>序号</th>
                    {columnHeader('仿真时间', 'time', 150)}
                    {columnHeader('仿真结果', 'result', 120, true)}
                    {columnHeader('场景数量', 'scenarioCount', 150, true)}
                    {columnHeader('模式', 'mode', 125, true)}
                    {columnHeader('仿真场景', 'scenario', 180)}
                    {columnHeader('来源', 'source', 125, true)}
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
                      <td style={{ ...cellStyle, width: 42, padding: 0 }}>
                        <input
                          type="checkbox"
                          aria-label={`选择第 ${safePage * pageSize + index + 1} 条 ${row.scenario}`}
                          disabled={selectionDisabled || !row.raw.deletion_allowed || !row.raw.deletion_target}
                          checked={selectedEvidenceIds.has(row.raw.evidence_id)}
                          onChange={() => toggleSelected(row.raw)}
                        />
                      </td>
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
                          border: row.outcome === 'failed'
                            ? '1px solid rgba(255, 91, 112, 0.55)'
                            : row.outcome === 'passed'
                              ? '1px solid rgba(69, 211, 207, 0.48)'
                              : '1px solid var(--line-2)',
                          background: row.outcome === 'failed'
                            ? 'rgba(255, 91, 112, 0.1)'
                            : row.outcome === 'passed'
                              ? 'rgba(69, 211, 207, 0.08)'
                              : 'rgba(255, 255, 255, 0.025)',
                          color: row.outcome === 'failed'
                            ? 'var(--c-danger)'
                            : row.outcome === 'passed'
                              ? 'var(--c-stbd)'
                              : 'var(--txt-3)',
                          fontWeight: 700,
                        }}>
                          {row.resultDisplay}
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
                            disabled={deleteActionsDisabled || !hasOverview(row.raw)}
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
                              cursor: deleteActionsDisabled ? 'wait' : hasOverview(row.raw) ? 'pointer' : 'not-allowed',
                            }}
                          >
                            <LucideImage size={13} aria-hidden="true" />
                            概述
                          </button>
                          <button
                            type="button"
                            disabled={deleteActionsDisabled || !canOpen(row.raw)}
                            title="打开轨迹回放"
                            onClick={() => {
                              if (canOpen(row.raw)) onOpen(row.raw.evidence_id);
                            }}
                            style={{
                              ...actionButtonStyle,
                              border: '1px solid var(--line-2)',
                              background: canOpen(row.raw) ? 'rgba(69, 211, 207, 0.07)' : 'transparent',
                              color: canOpen(row.raw) ? 'var(--txt-1)' : 'var(--txt-3)',
                              cursor: deleteActionsDisabled ? 'wait' : canOpen(row.raw) ? 'pointer' : 'not-allowed',
                            }}
                          >
                            <LucidePlay size={13} aria-hidden="true" />
                            回放
                          </button>
                          <button
                            type="button"
                            aria-label={`删除 ${row.raw.session_id}`}
                            title={batchDeleteNeedsRescan
                              ? '请先扫描核对证据库'
                              : row.raw.deletion_allowed && row.raw.deletion_target
                                ? `删除 ${row.raw.session_id}`
                                : row.raw.deletion_error || '删除目标不安全'}
                            disabled={destructiveActionsDisabled || !row.raw.deletion_allowed || !row.raw.deletion_target}
                            onClick={(event) => openDeleteDialog(row.raw, event.currentTarget)}
                            style={{
                              ...actionButtonStyle,
                              border: '1px solid rgba(255, 91, 112, 0.52)',
                              background: 'rgba(255, 91, 112, 0.08)',
                              color: 'var(--c-danger)',
                              cursor: destructiveActionsDisabled
                                ? 'not-allowed'
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
                disabled={deleteActionsDisabled}
                onChange={(event) => {
                  clearSelection();
                  setPageSize(Number(event.target.value) as 20 | 50);
                  setPage(0);
                }}
                style={{ ...filterStyle, width: 72, marginTop: 0 }}
              >
                <option value={20}>20</option>
                <option value={50}>50</option>
              </select>
              <span>{safePage + 1} / {totalPages}</span>
              <button type="button" aria-label="上一页" disabled={deleteActionsDisabled || safePage <= 0} onClick={() => setPage(safePage - 1)} style={headerButtonStyle}>‹</button>
              <button type="button" aria-label="下一页" disabled={deleteActionsDisabled || safePage >= totalPages - 1} onClick={() => setPage(safePage + 1)} style={headerButtonStyle}>›</button>
            </div>
          </>
        )}
      </main>
      {pendingBatchDelete && (
        <div
          ref={batchDeleteDialogRef}
          role="dialog"
          aria-label="批量删除仿真记录"
          aria-modal="true"
          onKeyDown={handleBatchDeleteDialogKeyDown}
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
          onClick={closeBatchDeleteDialog}
        >
          <div
            style={{
              width: 'min(620px, 100%)',
              maxHeight: 'min(720px, 90vh)',
              overflow: 'auto',
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
              <h2 style={{ margin: 0, fontSize: 15, letterSpacing: 0 }}>
                {batchDeleteResult
                  ? '批量删除结果'
                  : batchDeleteNeedsRescan ? '批量删除状态未知' : '批量删除仿真记录'}
              </h2>
            </div>
            {batchDeleteResult ? (
              <div
                role="alert"
                style={{
                  marginTop: 16,
                  padding: '11px 12px',
                  border: '1px solid rgba(255, 91, 112, 0.5)',
                  borderRadius: 4,
                  background: 'rgba(255, 91, 112, 0.08)',
                  color: 'var(--txt-2)',
                  fontSize: 11,
                  lineHeight: 1.6,
                }}
              >
                <div style={{ display: 'flex', gap: 16, color: 'var(--txt-1)', fontWeight: 700 }}>
                  <span>已删除 {batchDeleteResult.deleted}</span>
                  <span style={{ color: 'var(--c-danger)' }}>失败 {batchDeleteResult.failed}</span>
                  {batchDeleteCleanupPending.length > 0 && (
                    <span>文件清理待处理 {batchDeleteCleanupPending.length}</span>
                  )}
                </div>
                {(batchDeleteFailures.length > 0 || batchDeleteCleanupPending.length > 0) && (
                  <ul style={{ margin: '10px 0 0', paddingLeft: 18 }}>
                    {batchDeleteFailures.map((item) => (
                      <li key={item.evidence_id} style={{ marginTop: 6, overflowWrap: 'anywhere' }}>
                        <code style={{ color: 'var(--txt-1)' }}>{item.evidence_id}</code>: {item.error}
                      </li>
                    ))}
                    {batchDeleteCleanupPending.map((item) => (
                      <li key={item.evidence_id} style={{ marginTop: 6, overflowWrap: 'anywhere' }}>
                        <code style={{ color: 'var(--txt-1)' }}>{item.evidence_id}</code>: {item.cleanup_error}
                        {item.cleanup_path && (
                          <div>
                            暂存路径：<code style={{ color: 'var(--txt-1)' }}>{item.cleanup_path}</code>
                          </div>
                        )}
                        {item.cleanup_metadata_path && (
                          <div>
                            恢复元数据：<code style={{ color: 'var(--txt-1)' }}>{item.cleanup_metadata_path}</code>
                          </div>
                        )}
                        {additionalCleanupPaths(item).map((path) => (
                          <div key={path}>
                            待清理路径：<code style={{ color: 'var(--txt-1)' }}>{path}</code>
                          </div>
                        ))}
                        {item.cleanup_path && (
                          <button
                            type="button"
                            aria-label={`确认已记录 ${item.evidence_id}`}
                            onClick={() => acknowledgePendingCleanup(item.cleanup_path!)}
                            style={{ ...actionButtonStyle, marginTop: 5 }}
                          >
                            已记录
                          </button>
                        )}
                      </li>
                    ))}
                  </ul>
                )}
              </div>
            ) : (
              <>
                <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8, marginTop: 16, color: 'var(--txt-2)', fontSize: 11 }}>
                  <span>共 {pendingBatchDelete.length} 条</span>
                  <span>通过 {batchDeleteSummary.passed}</span>
                  <span>不通过 {batchDeleteSummary.failed}</span>
                  <span>未知 {batchDeleteSummary.unknown}</span>
                  <span>工作树 {batchDeleteSummary.worktrees}</span>
                </div>
                <p style={{ margin: '14px 0 0', color: 'var(--c-danger)', fontSize: 12, lineHeight: 1.6 }}>
                  此操作将从数据库与文件系统永久删除所选证据，无法撤销。
                </p>
              </>
            )}
            {batchDeleteNeedsRescan && !batchDeleteResult && (
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
                未收到批量删除结果。为避免重复删除，不能直接重试；请关闭后扫描核对证据库。
              </div>
            )}
            {batchDeletePersistenceFailed && !batchDeleteResult && (
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
                清理记录持久化不可用，尚未发送批量删除请求。请关闭对话框，扫描恢复后重新选择并重试。
              </div>
            )}
            <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8, marginTop: 18 }}>
              <button
                ref={batchDeleteCancelButtonRef}
                type="button"
                onClick={closeBatchDeleteDialog}
                disabled={batchDeleteState.isLoading}
                style={{
                  ...actionButtonStyle,
                  minWidth: 72,
                  border: '1px solid var(--line-2)',
                  background: 'rgba(255, 255, 255, 0.025)',
                  color: 'var(--txt-2)',
                  cursor: batchDeleteState.isLoading ? 'not-allowed' : 'pointer',
                }}
              >
                {batchDeleteResult || batchDeleteNeedsRescan || batchDeletePersistenceFailed ? '关闭' : '取消'}
              </button>
              {!batchDeleteNeedsRescan && (!batchDeleteResult || batchDeleteFailures.length > 0) && (
                <button
                  type="button"
                  onClick={() => void handleBatchDelete()}
                  disabled={destructiveActionsDisabled}
                  style={{
                    ...actionButtonStyle,
                    minWidth: 118,
                    border: '1px solid var(--c-danger)',
                    background: 'rgba(255, 91, 112, 0.13)',
                    color: 'var(--c-danger)',
                    cursor: destructiveActionsDisabled ? 'wait' : 'pointer',
                  }}
                >
                  <LucideTrash2 size={13} aria-hidden="true" />
                  {batchDeleteState.isLoading
                    ? '删除中'
                    : batchDeleteResult
                      ? `重试失败项（${batchDeleteFailures.length}）`
                      : '确认批量删除'}
                </button>
              )}
            </div>
          </div>
        </div>
      )}
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
            {(deleteFailed || deletePersistenceFailed) && (
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
                {deletePersistenceFailed
                  ? '清理记录持久化不可用，尚未发送删除请求。请关闭对话框，扫描恢复后重新发起删除。'
                  : '删除失败，请保留记录后重试。'}
              </div>
            )}
            <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 8, marginTop: 18 }}>
              <button
                ref={deleteCancelButtonRef}
                type="button"
                onClick={cancelDelete}
                disabled={deleteActionsDisabled}
                style={{
                  ...actionButtonStyle,
                  minWidth: 72,
                  border: '1px solid var(--line-2)',
                  background: 'rgba(255, 255, 255, 0.025)',
                  color: 'var(--txt-2)',
                  cursor: deleteActionsDisabled ? 'not-allowed' : 'pointer',
                }}
              >
                {deletePersistenceFailed ? '关闭' : '取消'}
              </button>
              <button
                type="button"
                onClick={() => void handleDelete()}
                disabled={destructiveActionsDisabled}
                style={{
                  ...actionButtonStyle,
                  minWidth: 96,
                  border: '1px solid var(--c-danger)',
                  background: 'rgba(255, 91, 112, 0.13)',
                  color: 'var(--c-danger)',
                  cursor: destructiveActionsDisabled ? 'wait' : 'pointer',
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
          aria-hidden={pendingDelete || pendingBatchDelete ? true : undefined}
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
