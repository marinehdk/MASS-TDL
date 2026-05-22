import { describe, it, expect, vi } from 'vitest';

const rosOn = vi.fn();
const rosClose = vi.fn();

vi.mock('@tier4/roslibjs-foxglove', () => ({
  Ros: vi.fn(() => ({ on: rosOn, close: rosClose })),
  Topic: vi.fn(() => ({ subscribe: vi.fn(), unsubscribe: vi.fn() })),
  default: undefined,
}));

vi.mock('@foxglove/ws-protocol', () => ({
  FoxgloveClient: class {
    static SUPPORTED_SUBPROTOCOL = 'foxglove.websocket.v1';
  },
}));

const mockSetters = Object.fromEntries(
  ['updateOwnShip','updateTargets','updateEnvironment','updateModulePulses',
   'appendAsdrEvent','updateLifecycleStatus','setWsConnected','updateScoringRow',
   'updateSensors','updateCommLinks','updateFaultStatus','updateControlCmd']
  .map(k => [k, vi.fn()])
);

vi.mock('../store', () => {
  const store = { useTelemetryStore: vi.fn((sel: any) => sel(mockSetters)) };
  (store.useTelemetryStore as any).getState = vi.fn(() => mockSetters);
  return store;
});

import { useFoxgloveLive } from './useFoxgloveLive';
import { renderHook } from '@testing-library/react';
import { Ros } from '@tier4/roslibjs-foxglove';

describe('useFoxgloveLive', () => {
  it('connects at ws://127.0.0.1:8765 by default', () => {
    renderHook(() => useFoxgloveLive());
    expect(Ros).toHaveBeenCalledWith(
      expect.objectContaining({ url: 'ws://127.0.0.1:8765' })
    );
  });

  it('accepts custom URL', () => {
    renderHook(() => useFoxgloveLive('ws://10.0.0.1:9999'));
    expect(Ros).toHaveBeenCalledWith(
      expect.objectContaining({ url: 'ws://10.0.0.1:9999' })
    );
  });

  it('registers connection, close, error handlers', () => {
    renderHook(() => useFoxgloveLive('ws://127.0.0.1:8765'));
    expect(rosOn).toHaveBeenCalledWith('connection', expect.any(Function));
    expect(rosOn).toHaveBeenCalledWith('close', expect.any(Function));
    expect(rosOn).toHaveBeenCalledWith('error', expect.any(Function));
  });

  it('subscription map has 11 topics', () => {
    const topics = [
      '/sil/own_ship_state','/sil/target_vessel_state','/sil/environment',
      '/sil/module_pulse','/sil/asdr_event','/sil/lifecycle_status',
      '/sil/scoring','/sil/sensor_status','/sil/commlink_status',
      '/sil/fault_status','/sil/control_cmd',
    ];
    expect(topics).toHaveLength(11);
  });

  it('closes Ros on unmount', () => {
    const { unmount } = renderHook(() => useFoxgloveLive('ws://127.0.0.1:8765'));
    unmount();
    expect(rosClose).toHaveBeenCalled();
  });
});
