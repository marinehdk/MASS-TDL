import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  useGetEvidenceLibrarySessionsQuery,
  useRescanEvidenceLibraryMutation,
  type EvidenceLibrarySession,
} from '../../api/silApi';

interface EvidenceLibraryViewProps {
  onOpen: (evidenceId: string) => void;
}

type SortKey = 'id' | 'time' | 'result' | 'suite' | 'mode' | 'scenario' | 'source' | 'worktree';
type SortDirection = 'asc' | 'desc';
type RefreshIntervalSeconds = 600 | 3600 | 86400;

interface SessionRow {
  raw: EvidenceLibrarySession;
  id: string;
  time: string;
  timeValue: number;
  result: string;
  suite: string;
  mode: string;
  scenario: string;
  source: string;
  worktree: string;
}

const compactId = (value: string) => (value.length > 12 ? `${value.slice(0, 8)}...` : value);

const displayRunTime = (value?: string | null) =>
  value ? value.replace('T', ' ').replace(/([+-]\d\d:\d\d|Z)$/, '') : '-';

const displayName = (session: EvidenceLibrarySession) => {
  const withoutTime = session.session_id.replace(/^\d{8}_\d{6}_?/, '');
  return withoutTime || session.session_id;
};

const scenarioCount = (session: EvidenceLibrarySession) =>
  session.scenario_ids?.length || session.scenario_count || 0;

const displaySuite = (session: EvidenceLibrarySession) => {
  const count = scenarioCount(session);
  return count <= 1 ? '单个场景' : `批量场景(${count})`;
};

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
  id: session.evidence_id,
  time: displayRunTime(session.created_at),
  timeValue: session.created_at ? Date.parse(session.created_at) || 0 : 0,
  result: displayResult(session),
  suite: displaySuite(session),
  mode: displayMode(session),
  scenario: displayScenario(session),
  source: displaySource(session),
  worktree: displayWorktree(session),
});

const compareRows = (a: SessionRow, b: SessionRow, key: SortKey) => {
  if (key === 'time') return a.timeValue - b.timeValue;
  return String(a[key]).localeCompare(String(b[key]), 'zh-Hans-CN', { numeric: true });
};

const uniqueValues = (rows: SessionRow[], key: SortKey) =>
  Array.from(new Set(rows.map((row) => String(row[key])).filter(Boolean))).sort((a, b) =>
    a.localeCompare(b, 'zh-Hans-CN', { numeric: true }),
  );

const cellStyle = {
  padding: '10px 8px',
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

const formatCountdown = (seconds: number) => {
  const clamped = Math.max(0, seconds);
  const hours = Math.floor(clamped / 3600);
  const minutes = Math.floor((clamped % 3600) / 60);
  const secs = clamped % 60;
  if (hours > 0) return `${hours}:${String(minutes).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;
  return `${minutes}:${String(secs).padStart(2, '0')}`;
};

export function EvidenceLibraryView({ onOpen }: EvidenceLibraryViewProps) {
  const { data, isLoading, refetch } = useGetEvidenceLibrarySessionsQuery();
  const [rescan, rescanState] = useRescanEvidenceLibraryMutation();
  const sessions = data?.sessions ?? [];
  const [sort, setSort] = useState<{ key: SortKey; direction: SortDirection }>({ key: 'time', direction: 'desc' });
  const [pageSize, setPageSize] = useState<20 | 50>(20);
  const [page, setPage] = useState(0);
  const [filters, setFilters] = useState<Partial<Record<SortKey, string>>>({});
  const [autoRefreshSeconds, setAutoRefreshSeconds] = useState<RefreshIntervalSeconds>(86400);
  const [countdownSeconds, setCountdownSeconds] = useState(86400);
  const [overviewSession, setOverviewSession] = useState<EvidenceLibrarySession | null>(null);
  const [overviewIndex, setOverviewIndex] = useState(0);
  const [overviewScale, setOverviewScale] = useState(1);
  const [overviewOffset, setOverviewOffset] = useState({ x: 0, y: 0 });
  const [overviewDrag, setOverviewDrag] = useState<{ startX: number; startY: number; originX: number; originY: number } | null>(null);

  const rows = useMemo(() => sessions.map(toRow), [sessions]);
  const filteredRows = useMemo(() => {
    const activeFilters = Object.entries(filters).filter(([, value]) => value);
    return rows.filter((row) => activeFilters.every(([key, value]) => String(row[key as SortKey]) === value));
  }, [filters, rows]);
  const sortedRows = useMemo(() => {
    return [...filteredRows].sort((a, b) => {
      const result = compareRows(a, b, sort.key);
      return sort.direction === 'asc' ? result : -result;
    });
  }, [filteredRows, sort]);
  const totalPages = Math.max(1, Math.ceil(sortedRows.length / pageSize));
  const safePage = Math.min(page, totalPages - 1);
  const visibleRows = sortedRows.slice(safePage * pageSize, safePage * pageSize + pageSize);
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

  const changeOverviewImage = (delta: number) => {
    if (overviewPngs.length <= 1) return;
    setOverviewIndex((current) => (current + delta + overviewPngs.length) % overviewPngs.length);
    resetOverviewView();
  };

  const zoomOverview = (delta: number) => {
    setOverviewScale((current) => Math.max(0.5, Math.min(4, Number((current + delta).toFixed(2)))));
  };

  const handleRescan = useCallback(async () => {
    await rescan({ force: false });
    await refetch();
    setCountdownSeconds(autoRefreshSeconds);
  }, [autoRefreshSeconds, refetch, rescan]);

  useEffect(() => {
    setCountdownSeconds(autoRefreshSeconds);
  }, [autoRefreshSeconds]);

  useEffect(() => {
    const timer = window.setInterval(() => {
      setCountdownSeconds((current) => {
        if (current <= 1) {
          void handleRescan();
          return autoRefreshSeconds;
        }
        return current - 1;
      });
    }, 1000);
    return () => window.clearInterval(timer);
  }, [autoRefreshSeconds, handleRescan]);

  const toggleSort = (key: SortKey) => {
    setSort((current) => ({
      key,
      direction: current.key === key && current.direction === 'desc' ? 'asc' : 'desc',
    }));
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

  const columnHeader = (
    label: string,
    key: SortKey,
    width: number,
    filter?: 'enum',
  ) => (
    <th align="center" style={{ width, padding: '8px 8px 7px', verticalAlign: 'middle', textAlign: 'center' }}>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6, minHeight: 24 }}>
        <span style={{ flex: 1, textAlign: 'center' }}>{label}</span>
        {filter === 'enum' ? (
          enumFilter(key)
        ) : (
          <button type="button" onClick={() => toggleSort(key)} style={headerButtonStyle} title={`按${label}排序`}>
            {sort.key === key ? (sort.direction === 'asc' ? '↑' : '↓') : '↕'}
          </button>
        )}
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
      <main style={{ flex: 1, padding: 12, overflow: 'hidden', display: 'flex', flexDirection: 'column', minWidth: 0 }}>
        {isLoading ? (
          <div>Loading evidence</div>
        ) : (
          <>
            <div style={{
              display: 'flex',
              alignItems: 'center',
              gap: 12,
              padding: '0 0 10px',
              fontFamily: 'var(--f-mono)',
              position: 'relative',
              minHeight: 34,
            }}>
              <div style={{
                display: 'flex',
                gap: 16,
                color: 'var(--txt-3)',
                fontSize: 12,
              }}>
                <span>记录数: {sessions.length}</span>
                <span>显示: {sortedRows.length}</span>
              </div>
              <h1 style={{
                position: 'absolute',
                left: '50%',
                transform: 'translateX(-50%)',
                fontFamily: 'var(--f-disp)',
                fontSize: 15,
                fontWeight: 800,
                letterSpacing: '0.08em',
                color: 'var(--txt-1)',
                margin: 0,
              }}>
                仿真数据库
              </h1>
              <div style={{
                marginLeft: 'auto',
                display: 'flex',
                alignItems: 'center',
                gap: 8,
                color: 'var(--txt-3)',
                fontSize: 12,
              }}>
                <button
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
                    lineHeight: '24px',
                    cursor: rescanState.isLoading ? 'wait' : 'pointer',
                  }}
                >
                  {rescanState.isLoading ? '刷新中' : '刷新'}
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
                }}>
                  {formatCountdown(countdownSeconds)}
                </span>
              </div>
            </div>
            <div style={{
              flex: 1,
              minHeight: 0,
              overflow: 'auto',
              border: '1px solid var(--line-2)',
              background: 'linear-gradient(180deg, rgba(18, 29, 44, 0.72), rgba(7, 12, 19, 0.9))',
            }}>
              <table style={{ width: '100%', minWidth: 1420, tableLayout: 'fixed', borderCollapse: 'collapse', fontFamily: 'var(--f-mono)', fontSize: 12 }}>
                <thead style={{ position: 'sticky', top: 0, zIndex: 2, background: 'rgba(13, 23, 35, 0.96)' }}>
                  <tr style={{ color: 'var(--txt-3)', borderBottom: '1px solid var(--line-2)' }}>
                    {columnHeader('会话ID', 'id', 135)}
                    {columnHeader('仿真时间', 'time', 150)}
                    {columnHeader('仿真结果', 'result', 120)}
                    {columnHeader('套件', 'suite', 125, 'enum')}
                    {columnHeader('模式', 'mode', 125, 'enum')}
                    {columnHeader('仿真场景', 'scenario', 190)}
                    {columnHeader('来源', 'source', 90, 'enum')}
                    {columnHeader('工作树', 'worktree', 185)}
                    <th align="center" style={{ width: 130, padding: '8px 8px 7px', verticalAlign: 'middle', textAlign: 'center' }}>打开</th>
                  </tr>
                </thead>
                <tbody>
                  {visibleRows.map((row, index) => (
                    <tr
                      key={row.raw.evidence_id}
                      style={{
                        borderTop: '1px solid rgba(78, 108, 139, 0.24)',
                        background: index % 2 === 0 ? 'rgba(255, 255, 255, 0.012)' : 'transparent',
                      }}
                    >
                      <td style={cellStyle} title={row.id}>{compactId(row.id)}</td>
                      <td style={cellStyle} title={row.raw.created_at ?? ''}>{row.time}</td>
                      <td style={{
                        ...cellStyle,
                        color: row.result === '不通过' ? 'var(--c-danger)' : row.result === '通过' || row.result.includes('/' ) ? 'var(--c-stbd)' : 'var(--txt-3)',
                        fontWeight: 700,
                      }}>{row.result}</td>
                      <td style={cellStyle}>{row.suite}</td>
                      <td style={cellStyle}>{row.mode}</td>
                      <td style={cellStyle} title={(row.raw.scenario_ids ?? []).join(', ')}>{row.scenario}</td>
                      <td style={cellStyle}>{row.source}</td>
                      <td style={cellStyle} title={row.worktree}>{row.worktree}</td>
                      <td style={{ ...cellStyle, display: 'flex', justifyContent: 'center', gap: 6 }}>
                        <button
                          disabled={!row.raw.overview_png}
                          onClick={() => {
                            if (row.raw.overview_png) {
                              setOverviewSession(row.raw);
                              setOverviewIndex(0);
                              resetOverviewView();
                            }
                          }}
                          style={{
                            border: '1px solid var(--line-2)',
                            background: row.raw.overview_png ? 'rgba(69, 211, 207, 0.08)' : 'transparent',
                            color: row.raw.overview_png ? 'var(--txt-1)' : 'var(--txt-3)',
                            padding: '4px 8px',
                            cursor: row.raw.overview_png ? 'pointer' : 'not-allowed',
                          }}
                        >
                          概述
                        </button>
                        <button
                          disabled={!canOpen(row.raw)}
                          onClick={() => {
                            if (canOpen(row.raw)) onOpen(row.raw.evidence_id);
                          }}
                          style={{
                            border: '1px solid var(--line-2)',
                            background: canOpen(row.raw) ? 'rgba(69, 211, 207, 0.08)' : 'transparent',
                            color: canOpen(row.raw) ? 'var(--txt-1)' : 'var(--txt-3)',
                            padding: '4px 10px',
                            cursor: canOpen(row.raw) ? 'pointer' : 'not-allowed',
                          }}
                        >
                          回放
                        </button>
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
              <button type="button" disabled={safePage <= 0} onClick={() => setPage(safePage - 1)} style={headerButtonStyle}>‹</button>
              <button type="button" disabled={safePage >= totalPages - 1} onClick={() => setPage(safePage + 1)} style={headerButtonStyle}>›</button>
            </div>
          </>
        )}
      </main>
      {overviewSession && (
        <div
          role="dialog"
          aria-label="仿真概述"
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
