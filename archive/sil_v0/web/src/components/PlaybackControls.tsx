import { useState, useRef, useEffect, memo } from 'react';

type JobStatus = 'IDLE' | 'RUNNING' | 'DONE' | 'ERROR';

interface Props {
  scenarioId: string;
  disabled: boolean;
}

const STATUS_COLORS: Record<JobStatus, string> = {
  IDLE: 'var(--text-2)',
  RUNNING: '#f59e0b',
  DONE: 'var(--color-stbd)',
  ERROR: 'var(--color-port)',
};

export const PlaybackControls = memo(function PlaybackControls({ scenarioId, disabled }: Props) {
  const [jobStatus, setJobStatus] = useState<JobStatus>('IDLE');
  const [jobId, setJobId] = useState<string | null>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  useEffect(() => {
    return () => { if (pollRef.current) clearInterval(pollRef.current); };
  }, []);

  const play = async () => {
    setJobStatus('RUNNING');
    const res = await fetch('/sil/scenario/run', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ scenario_ids: [scenarioId] }),
    });
    const { job_id } = await res.json();
    setJobId(job_id);
    pollRef.current = setInterval(async () => {
      const r = await fetch(`/sil/scenario/status/${job_id}`);
      const { status } = await r.json();
      if (status === 'done') { setJobStatus('DONE'); clearInterval(pollRef.current!); }
      else if (status === 'failed') { setJobStatus('ERROR'); clearInterval(pollRef.current!); }
    }, 1000);
  };

  const reset = () => {
    if (pollRef.current) clearInterval(pollRef.current);
    setJobStatus('IDLE');
    setJobId(null);
  };

  const btn = (label: string, action: () => void, enable: boolean) => (
    <button onClick={action} disabled={!enable} style={{
      fontFamily: 'var(--fnt-mono)',
      fontSize: '10px',
      padding: '4px 10px',
      background: enable ? 'var(--bg-2)' : 'var(--bg-1)',
      color: enable ? 'var(--text-1)' : 'var(--text-3)',
      border: '1px solid var(--line-1)',
      borderRadius: 'var(--radius-none)',
      cursor: enable ? 'pointer' : 'default',
    }}>{label}</button>
  );

  return (
    <div style={{
      padding: '10px 14px',
      borderBottom: '1px solid var(--line-1)',
      display: 'flex',
      alignItems: 'center',
      gap: '6px',
    }}>
      {btn('▶ Play', play, !disabled && jobStatus !== 'RUNNING')}
      {btn('⏹ Reset', reset, jobStatus !== 'IDLE')}
      <span style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '9px',
        color: STATUS_COLORS[jobStatus],
        marginLeft: 'auto',
      }}>
        {jobStatus}
      </span>
    </div>
  );
});
