import { useState, useCallback, useEffect, useRef } from 'react';
import type { GateSSEEvent, SSECompleteEvent } from '../types/gateStream';

export interface UseGateStreamReturn {
  gates: GateSSEEvent[];
  verdict: 'GO' | 'NO-GO' | null;
  streaming: boolean;
  error: string | null;
  start: () => void;
  abort: () => void;
}

export function useGateStream(scenarioId: string | null, autoStart = true): UseGateStreamReturn {
  const [gates, setGates] = useState<GateSSEEvent[]>([]);
  const [verdict, setVerdict] = useState<'GO' | 'NO-GO' | null>(null);
  const [streaming, setStreaming] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const esRef = useRef<EventSource | null>(null);

  const cleanup = useCallback(() => {
    esRef.current?.close();
    esRef.current = null;
    setStreaming(false);
  }, []);

  const start = useCallback(() => {
    if (!scenarioId) return;
    cleanup();
    setGates([]);
    setVerdict(null);
    setError(null);
    setStreaming(true);
    const es = new EventSource(`/api/v1/selfcheck/stream?scenario_id=${encodeURIComponent(scenarioId)}`);
    esRef.current = es;
    es.onmessage = (e: MessageEvent) => {
      try {
        const data = JSON.parse(e.data);
        if (data.type === 'complete') {
          setVerdict((data as SSECompleteEvent).go_no_go);
          setStreaming(false);
          es.close();
          esRef.current = null;
        } else {
          setGates(prev => [...prev, data as GateSSEEvent]);
        }
      } catch (err) {
        setError(`Parse error: ${(err as Error).message}`);
      }
    };
    es.onerror = () => {
      setError('SSE connection lost');
      setStreaming(false);
      es.close();
      esRef.current = null;
    };
  }, [scenarioId, cleanup]);

  const abort = useCallback(() => {
    cleanup();
    setGates([]);
    setVerdict(null);
    setError(null);
  }, [cleanup]);

  useEffect(() => {
    if (autoStart && scenarioId) start();
    return cleanup;
  }, [scenarioId, autoStart, start, cleanup]);

  return { gates, verdict, streaming, error, start, abort };
}
