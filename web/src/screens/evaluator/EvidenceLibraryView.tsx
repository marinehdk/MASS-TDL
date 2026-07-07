import {
  useGetEvidenceLibrarySessionsQuery,
  useRescanEvidenceLibraryMutation,
  type EvidenceLibrarySession,
} from '../../api/silApi';

interface EvidenceLibraryViewProps {
  onOpen: (evidenceId: string) => void;
}

const statusColor = (status?: string | null) => {
  if (status === 'ok' || status === 'completed' || status === 'indexed') return 'var(--c-stbd)';
  if (status === 'failed' || status === 'error') return 'var(--c-danger)';
  return 'var(--txt-3)';
};

const displayWorktree = (session: EvidenceLibrarySession) =>
  session.worktree_name || session.branch || '-';

export function EvidenceLibraryView({ onOpen }: EvidenceLibraryViewProps) {
  const { data, isLoading, refetch } = useGetEvidenceLibrarySessionsQuery();
  const [rescan, rescanState] = useRescanEvidenceLibraryMutation();
  const sessions = data?.sessions ?? [];

  const handleRescan = async () => {
    await rescan({ force: false });
    await refetch();
  };

  return (
    <div style={{
      height: '100%',
      display: 'grid',
      gridTemplateColumns: '280px 1fr',
      background: 'var(--bg-0)',
      color: 'var(--txt-1)',
    }}>
      <aside style={{
        borderRight: '1px solid var(--line-1)',
        padding: 16,
        display: 'flex',
        flexDirection: 'column',
        gap: 12,
      }}>
        <h1 style={{ fontFamily: 'var(--f-disp)', fontSize: 18, margin: 0 }}>
          Evidence Library
        </h1>
        <button
          onClick={handleRescan}
          disabled={rescanState.isLoading}
          style={{
            border: '1px solid var(--c-phos)',
            color: 'var(--c-phos)',
            background: 'transparent',
            padding: '8px 10px',
            cursor: rescanState.isLoading ? 'wait' : 'pointer',
          }}
        >
          {rescanState.isLoading ? 'Rescanning' : 'Rescan'}
        </button>
        <div style={{ fontFamily: 'var(--f-mono)', fontSize: 11, color: 'var(--txt-3)' }}>
          Sessions: {sessions.length}
        </div>
      </aside>
      <main style={{ padding: 16, overflow: 'auto' }}>
        {isLoading ? (
          <div>Loading evidence</div>
        ) : (
          <table style={{ width: '100%', borderCollapse: 'collapse', fontFamily: 'var(--f-mono)', fontSize: 12 }}>
            <thead>
              <tr style={{ color: 'var(--txt-3)' }}>
                <th align="left">Session</th>
                <th align="left">Source</th>
                <th align="left">Root</th>
                <th align="left">Worktree</th>
                <th align="left">Status</th>
                <th align="left">Action</th>
              </tr>
            </thead>
            <tbody>
              {sessions.map((session) => (
                <tr key={session.evidence_id} style={{ borderTop: '1px solid var(--line-1)' }}>
                  <td style={{ padding: '10px 6px' }}>{session.session_id}</td>
                  <td>{session.source}</td>
                  <td>{session.root_id}</td>
                  <td>{displayWorktree(session)}</td>
                  <td style={{ color: statusColor(session.ingest_status || session.status) }}>
                    {session.ingest_status || session.status || 'unknown'}
                  </td>
                  <td>
                    <button
                      onClick={() => onOpen(session.evidence_id)}
                      style={{
                        border: '1px solid var(--line-2)',
                        background: 'transparent',
                        color: 'var(--txt-1)',
                        padding: '4px 8px',
                        cursor: 'pointer',
                      }}
                    >
                      Open Replay
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </main>
    </div>
  );
}
