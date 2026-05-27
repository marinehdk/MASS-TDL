import { describe, it, expect, vi, beforeEach } from 'vitest';
import { renderHook, act } from '@testing-library/react';
import { useTelemetryStore } from '../store/telemetryStore';
import { useGateStream } from './useGateStream';

class MockEventSource {
  onmessage: ((e: MessageEvent) => void) | null = null;
  onerror: (() => void) | null = null;
  close = vi.fn();
  static lastInstance: MockEventSource | null = null;
  constructor(public url: string) {
    MockEventSource.lastInstance = this;
  }
}
(globalThis as any).EventSource = MockEventSource;

describe('useGateStream', () => {
  beforeEach(() => { vi.clearAllMocks(); });

  it('starts streaming on mount with autoStart=true', () => {
    const { result } = renderHook(() => useGateStream('test-scenario', true));
    expect(result.current.streaming).toBe(true);
    expect(result.current.gates).toEqual([]);
    expect(result.current.verdict).toBeNull();
  });

  it('does not auto-start when autoStart=false', () => {
    const { result } = renderHook(() => useGateStream('test-scenario', false));
    expect(result.current.streaming).toBe(false);
  });

  it('returns null scenario handling', () => {
    const { result } = renderHook(() => useGateStream(null, true));
    expect(result.current.streaming).toBe(false);
  });

  it('correctly appends and formats log messages for Option A and collapses UNSPECIFIED logs on Gate 2', () => {
    useTelemetryStore.getState().reset();
    
    renderHook(() => useGateStream('test-scenario', true));
    
    const instance = MockEventSource.lastInstance;
    expect(instance).not.toBeNull();
    expect(instance?.onmessage).not.toBeNull();
    
    // 1. Test Gate 1 format: >>> [SYS] GATE 1 ...
    act(() => {
      instance?.onmessage?.({
        data: JSON.stringify({
          gate_id: 1,
          label: "System Readiness",
          passed: true,
          duration_ms: 120,
          rationale: "all 5/5 sub-checks passed",
          checks: [
            { status: "ok", item: "docker compose", detail: "docker CLI not in PATH" },
            { status: "ok", item: "ROS2 DDS", detail: "23 ROS2 nodes visible" }
          ]
        })
      } as MessageEvent);
    });
    
    let logs = useTelemetryStore.getState().preflightLog;
    expect(logs.length).toBe(4); // Start, check 1, check 2, Success
    expect(logs[0].message).toContain('>>> [SYS] GATE 1: SYSTEM READINESS 检测中...');
    expect(logs[1].message).toContain('  * [OK] docker CLI not in PATH');
    expect(logs[2].message).toContain('  * [OK] 23 ROS2 nodes visible');
    expect(logs[3].message).toContain('<<< [SYS] GATE 1: SUCCESS (耗时 120 ms) - all 5/5 sub-checks passed');
    
    // Reset logs for Gate 2 test
    useTelemetryStore.setState({ preflightLog: [] });
    
    // 2. Test Gate 2 unspecified warnings collapsing
    act(() => {
      instance?.onmessage?.({
        data: JSON.stringify({
          gate_id: 2,
          label: "Module Health (M1-M8)",
          passed: true,
          duration_ms: 330,
          rationale: "Phase 3: 8/8 modules UNSPECIFIED (L3 kernel undetected)",
          checks: [
            { status: "warn", item: "M1", detail: "M1: UNSPECIFIED latency=0ms drops=0 (Phase 3: L3 kernel nodes undetected)" },
            { status: "warn", item: "M2", detail: "M2: UNSPECIFIED latency=0ms drops=0 (Phase 3: L3 kernel nodes undetected)" },
            { status: "warn", item: "M3", detail: "M3: UNSPECIFIED latency=0ms drops=0 (Phase 3: L3 kernel nodes undetected)" },
            { status: "warn", item: "M4", detail: "M4: UNSPECIFIED latency=0ms drops=0 (Phase 3: L3 kernel nodes undetected)" },
            { status: "warn", item: "M5", detail: "M5: UNSPECIFIED latency=0ms drops=0 (Phase 3: L3 kernel nodes undetected)" },
            { status: "warn", item: "M6", detail: "M6: UNSPECIFIED latency=0ms drops=0 (Phase 3: L3 kernel nodes undetected)" },
            { status: "warn", item: "M7", detail: "M7: UNSPECIFIED latency=0ms drops=0 (Phase 3: L3 kernel nodes undetected)" },
            { status: "warn", item: "M8", detail: "M8: UNSPECIFIED latency=0ms drops=0 (Phase 3: L3 kernel nodes undetected)" },
            { status: "ok", item: "M7 isolation", detail: "M7 process not running (no containerized deployment)" }
          ]
        })
      } as MessageEvent);
    });
    
    logs = useTelemetryStore.getState().preflightLog;
    expect(logs.length).toBe(4);
    expect(logs[0].message).toContain('>>> [SYS] GATE 2: MODULE HEALTH (M1-M8) 检测中...');
    expect(logs[1].message).toContain('  * [OK] M7 process not running');
    expect(logs[2].message).toContain('  * [WARN] M1-M8 pulses: 8/8 modules UNSPECIFIED (Phase 3: L3 kernel nodes undetected)');
    expect(logs[3].message).toContain('<<< [SYS] GATE 2: SUCCESS (耗时 330 ms) - Phase 3: 8/8 modules UNSPECIFIED (L3 kernel undetected)');
  });
});

