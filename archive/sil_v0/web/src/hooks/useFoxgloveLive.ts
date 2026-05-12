import { useEffect, useRef } from 'react';
import { useTelemetryStore } from '../store';

export function useFoxgloveLive(wsUrl = 'ws://localhost:8765') {
  const wsRef = useRef<WebSocket | null>(null);
  const updateOwnShip = useTelemetryStore((s) => s.updateOwnShip);
  const updateTargets = useTelemetryStore((s) => s.updateTargets);
  const updateEnvironment = useTelemetryStore((s) => s.updateEnvironment);
  const updateModulePulses = useTelemetryStore((s) => s.updateModulePulses);

  useEffect(() => {
    const ws = new WebSocket(wsUrl);
    wsRef.current = ws;

    ws.onopen = () => console.log('[Foxglove] WS connected');

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        switch (msg.topic) {
          case '/sil/own_ship_state':
            updateOwnShip(msg.payload);
            break;
          case '/sil/target_vessel_state':
            updateTargets(Array.isArray(msg.payload) ? msg.payload : [msg.payload]);
            break;
          case '/sil/environment_state':
            updateEnvironment(msg.payload);
            break;
          case '/sil/module_pulse':
            updateModulePulses(Array.isArray(msg.payload) ? msg.payload : [msg.payload]);
            break;
        }
      } catch {
        // Ignore malformed messages
      }
    };

    ws.onerror = () => console.warn('[Foxglove] WS error');
    ws.onclose = () => console.log('[Foxglove] WS disconnected');

    return () => { ws.close(); };
  }, [wsUrl, updateOwnShip, updateTargets, updateEnvironment, updateModulePulses]);

  return wsRef;
}
