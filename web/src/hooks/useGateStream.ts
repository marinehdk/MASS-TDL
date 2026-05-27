import { useState, useCallback, useEffect, useRef } from 'react';
import type { GateSSEEvent, SSECompleteEvent } from '../types/gateStream';
import { useTelemetryStore } from '../store';
import type { ModulePulse } from '../types/sil/module_pulse';

// Gate 2 check detail format: "M1: GREEN latency=2ms drops=0"
// or "M2: UNSPECIFIED latency=0ms drops=0 (Phase 1: L3 kernel nodes not deployed)"
const STATE_LABEL_MAP: Record<string, number> = {
  GREEN: 1,
  AMBER: 2,
  RED: 3,
  UNSPECIFIED: 0,
};

function parseGate2ModulePulses(checks: Array<{ item: string; status: string; detail: string }>): ModulePulse[] {
  const pulses: ModulePulse[] = [];
  for (const c of checks) {
    const detail = c.detail || '';
    // Match "M<N>: <STATE> latency=<N>ms drops=<N>"
    const m = detail.match(/^(M\d+):\s+(GREEN|AMBER|RED|UNSPECIFIED)/);
    if (!m) continue;
    const moduleId = parseInt(m[1].slice(1), 10) as ModulePulse['moduleId'];
    const state = (STATE_LABEL_MAP[m[2]] ?? 0) as ModulePulse['state'];
    const latencyMatch = detail.match(/latency=(\d+)ms/);
    const dropsMatch = detail.match(/drops=(\d+)/);
    pulses.push({
      moduleId,
      state,
      latencyMs: latencyMatch ? parseInt(latencyMatch[1], 10) : 0,
      messageDrops: dropsMatch ? parseInt(dropsMatch[1], 10) : 0,
    });
  }
  return pulses;
}

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
    
    // Reset the preflight logs ring buffer when beginning a new run
    useTelemetryStore.setState({ preflightLog: [] });

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

          // Gate 2: parse real module pulse states into telemetry store so
          // ModuleReadinessGrid (Screen 2 right panel) and ModulePulseBar
          // (Screen 3 top) both reflect the actual DDS bus state.
          if (data.gate_id === 2 && Array.isArray(data.checks)) {
            const modulePulses = parseGate2ModulePulses(data.checks);
            if (modulePulses.length > 0) {
              useTelemetryStore.getState().updateModulePulses(modulePulses);
            }
          }

          // Extract gate checks and stream them to the LiveLogStore
          const appendLog = useTelemetryStore.getState().appendPreflightLog;
          const timestamp = new Date().toLocaleTimeString('zh-CN', { hour12: false });
          
          appendLog({
            timestamp,
            level: 'info',
            message: `>>> [SYS] GATE ${data.gate_id}: ${data.label.toUpperCase()} 检测中...`
          });
          
          if (data.checks && Array.isArray(data.checks)) {
            let unspecifiedCount = 0;
            
            data.checks.forEach((c: any) => {
              const status = (c.status || 'info').toUpperCase();
              const level = status === 'OK' ? 'info' : status === 'FAIL' ? 'error' : 'warn';
              const detailText = c.detail || c.item || JSON.stringify(c);
              
              // Option A compression: consolidate duplicate UNSPECIFIED logs on Gate 2
              if (data.gate_id === 2 && detailText.includes('UNSPECIFIED')) {
                unspecifiedCount++;
              } else {
                appendLog({
                  timestamp,
                  level,
                  message: `  * [${status}] ${detailText}`
                });
              }
            });
            
            // Output one single consolidated warning line if we collapsed unspecified checks
            if (unspecifiedCount > 0) {
              appendLog({
                timestamp,
                level: 'warn',
                message: `  * [WARN] M1-M8 pulses: ${unspecifiedCount}/8 modules UNSPECIFIED (Phase 3: L3 kernel nodes undetected)`
              });
            }
          }
          
          appendLog({
            timestamp,
            level: data.passed ? 'info' : 'error',
            message: `<<< [SYS] GATE ${data.gate_id}: ${data.passed ? 'SUCCESS' : 'FAILED'} (耗时 ${data.duration_ms} ms) - ${data.rationale}`
          });
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
