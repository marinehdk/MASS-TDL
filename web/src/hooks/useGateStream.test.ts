import { describe, it, expect, vi, beforeEach } from 'vitest';
import { renderHook } from '@testing-library/react';
import { useGateStream } from './useGateStream';

class MockEventSource {
  onmessage: ((e: MessageEvent) => void) | null = null;
  onerror: (() => void) | null = null;
  close = vi.fn();
  constructor(public url: string) {}
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
});
