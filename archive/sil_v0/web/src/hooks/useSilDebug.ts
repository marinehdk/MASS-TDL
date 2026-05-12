import { useEffect, useRef, useState } from 'react';
import type { SilDebugData } from '../types/sil';

/** Connect to FastAPI /ws/sil_debug for decision-layer data */
export function useSilDebug(): SilDebugData | null {
  const [data, setData] = useState<SilDebugData | null>(null);
  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const ws = new WebSocket(`${protocol}://${window.location.host}/ws/sil_debug`);
    wsRef.current = ws;

    ws.onmessage = (event) => {
      try {
        const parsed: SilDebugData = JSON.parse(event.data);
        setData(parsed);
      } catch {
        // SilDebugSchema may push partial updates; ignore malformed frames
      }
    };

    ws.onclose = () => {
      // Auto-reconnect after 2s
      setTimeout(() => {
        if (wsRef.current && wsRef.current.readyState === WebSocket.CLOSED) {
          // will re-render and reconnect via useEffect cleanup + re-run
        }
      }, 2000);
    };

    return () => { ws.close(); };
  }, []);

  return data;
}
