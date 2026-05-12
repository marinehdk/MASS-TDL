import { useEffect, useRef } from 'react';
import { useTelemetryStore } from '../store';
import type { ASDREvent, LifecycleStatus } from '../store/telemetryStore';

// Reconnect with exponential back-off (max 30 s) so DEMO-1 survives a
// foxglove_bridge restart without requiring a page refresh.
const BASE_DELAY_MS = 1_000;
const MAX_DELAY_MS  = 30_000;

export function useFoxgloveLive(wsUrl = 'ws://localhost:8765') {
  const wsRef     = useRef<WebSocket | null>(null);
  const delayRef  = useRef(BASE_DELAY_MS);
  const deadRef   = useRef(false); // set true on component unmount

  const updateOwnShip          = useTelemetryStore((s) => s.updateOwnShip);
  const updateTargets          = useTelemetryStore((s) => s.updateTargets);
  const updateEnvironment      = useTelemetryStore((s) => s.updateEnvironment);
  const updateModulePulses     = useTelemetryStore((s) => s.updateModulePulses);
  const appendAsdrEvent        = useTelemetryStore((s) => s.appendAsdrEvent);
  const updateLifecycleStatus  = useTelemetryStore((s) => s.updateLifecycleStatus);
  const setWsConnected         = useTelemetryStore((s) => s.setWsConnected);

  useEffect(() => {
    deadRef.current = false;

    function connect() {
      if (deadRef.current) return;
      console.log('[Foxglove] connecting to', wsUrl);
      const ws = new WebSocket(wsUrl);
      wsRef.current = ws;

      ws.onopen = () => {
        console.log('[Foxglove] WS connected');
        delayRef.current = BASE_DELAY_MS; // reset back-off on success
        setWsConnected(true);
      };

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data as string) as {
            topic: string;
            payload: unknown;
          };
          switch (msg.topic) {
            case '/sil/own_ship_state':
              updateOwnShip(msg.payload as any);
              break;
            case '/sil/target_vessel_state':
              updateTargets(
                Array.isArray(msg.payload) ? msg.payload : [msg.payload as any],
              );
              break;
            case '/sil/environment_state':
              updateEnvironment(msg.payload as any);
              break;
            case '/sil/module_pulse':
              updateModulePulses(
                Array.isArray(msg.payload) ? msg.payload : [msg.payload as any],
              );
              break;
            case '/sil/asdr_event':
              appendAsdrEvent(msg.payload as ASDREvent);
              break;
            case '/sil/lifecycle_status':
              updateLifecycleStatus(msg.payload as LifecycleStatus);
              break;
          }
        } catch {
          // Silently ignore malformed frames
        }
      };

      ws.onerror = () => {
        console.warn('[Foxglove] WS error');
      };

      ws.onclose = () => {
        setWsConnected(false);
        if (deadRef.current) return;
        console.log(`[Foxglove] WS closed — reconnecting in ${delayRef.current}ms`);
        setTimeout(() => {
          delayRef.current = Math.min(delayRef.current * 2, MAX_DELAY_MS);
          connect();
        }, delayRef.current);
      };
    }

    connect();

    return () => {
      deadRef.current = true;
      wsRef.current?.close();
      wsRef.current = null;
      setWsConnected(false);
    };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [wsUrl]);

  return wsRef;
}
