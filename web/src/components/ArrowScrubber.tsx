// web/src/components/ArrowScrubber.tsx
import { useEffect, useRef, useState, useCallback } from 'react';
import { tableFromIPC, type Table } from 'apache-arrow';
import { gsap } from 'gsap';

interface ArrowScrubberProps {
  runId: string;
  orchestratorBase?: string;
  onFrame?: (rows: Array<{ timestamp_ns: bigint; channel: string; payload_bytes: Uint8Array }>) => void;
}

export function ArrowScrubber({
  runId,
  orchestratorBase = 'http://localhost:8000',
  onFrame,
}: ArrowScrubberProps) {
  const [table, setTable] = useState<Table | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [progress, setProgress] = useState(0);
  const [durationNs, setDurationNs] = useState<bigint>(BigInt(0));
  const playheadRef = useRef<HTMLDivElement>(null);
  const trackRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!runId) return;
    setLoading(true);
    setError(null);
    fetch(`${orchestratorBase}/runs/${runId}/replay.arrow`)
      .then((r) => {
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        return r.arrayBuffer();
      })
      .then((buf) => {
        const t = tableFromIPC(new Uint8Array(buf));
        setTable(t);
        if (t.numRows > 0) {
          const tsCol = t.getChildAt(0)!;
          const first = tsCol.get(0) as bigint;
          const last = tsCol.get(t.numRows - 1) as bigint;
          setDurationNs(last - first);
        }
      })
      .catch((e) => setError(String(e)))
      .finally(() => setLoading(false));
  }, [runId, orchestratorBase]);

  const scrubToProgress = useCallback((p: number) => {
    if (!table || durationNs === BigInt(0)) return;
    setProgress(p);
    const tsCol = table.getChildAt(0)!;
    const first = tsCol.get(0) as bigint;
    const targetNs = first + BigInt(Math.round(p * Number(durationNs)));

    // Binary search for first row >= targetNs
    let lo = 0, hi = table.numRows - 1;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if ((tsCol.get(mid) as bigint) < targetNs) lo = mid + 1;
      else hi = mid;
    }

    if (onFrame) {
      const row = {
        timestamp_ns: tsCol.get(lo) as bigint,
        channel: table.getChildAt(1)!.get(lo) as string,
        payload_bytes: table.getChildAt(2)!.get(lo) as Uint8Array,
      };
      onFrame([row]);
    }
  }, [table, durationNs, onFrame]);

  const onMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    const track = trackRef.current;
    if (!track) return;

    const move = (ev: MouseEvent) => {
      const rect = track.getBoundingClientRect();
      const p = Math.max(0, Math.min(1, (ev.clientX - rect.left) / rect.width));
      if (playheadRef.current) {
        gsap.set(playheadRef.current, { left: `${p * 100}%` });
      }
      scrubToProgress(p);
    };
    const up = () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
  }, [scrubToProgress]);

  if (loading) return <div data-testid="arrow-scrubber-loading">Loading replay...</div>;
  if (error)   return <div data-testid="arrow-scrubber-error">Error: {error}</div>;
  if (!table)  return null;

  const minutes = Math.floor(Number(durationNs) / 60e9);
  const seconds = Math.floor((Number(durationNs) % 60e9) / 1e9);

  return (
    <div data-testid="arrow-scrubber" style={{ padding: '8px', userSelect: 'none' }}>
      <div style={{ fontSize: 12, color: '#aaa', marginBottom: 4 }}>
        Replay: {table.numRows} messages · {minutes}m{seconds}s
      </div>
      <div
        ref={trackRef}
        data-testid="arrow-scrubber-track"
        onMouseDown={onMouseDown}
        style={{
          position: 'relative', height: 12, background: '#333',
          borderRadius: 6, cursor: 'pointer',
        }}
      >
        <div
          ref={playheadRef}
          data-testid="arrow-scrubber-playhead"
          style={{
            position: 'absolute', top: -4, left: `${progress * 100}%`,
            width: 20, height: 20, background: '#00bfff',
            borderRadius: '50%', transform: 'translateX(-50%)',
            pointerEvents: 'none',
          }}
        />
        <div style={{
          position: 'absolute', top: 0, left: 0,
          height: '100%', background: '#007aff', borderRadius: 6,
          width: `${progress * 100}%`, pointerEvents: 'none',
        }} />
      </div>
    </div>
  );
}
