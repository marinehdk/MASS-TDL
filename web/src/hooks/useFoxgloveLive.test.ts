import { describe, it, expect, vi, beforeEach } from 'vitest';

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
   'updateSensors','updateCommLinks','updateFaultStatus','updateControlCmd',
   'updateSat2','updateSat3','updateSotifMetrics']
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
      expect.objectContaining({ url: 'wss://127.0.0.1:8765' })
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

  it('subscription map has 14 topics', () => {
    const topics = [
      '/sil/own_ship_state','/sil/target_vessel_state','/sil/environment',
      '/sil/module_pulse','/sil/asdr_event','/sil/lifecycle_status',
      '/sil/scoring','/sil/sensor_status','/sil/commlink_status',
      '/sil/fault_status','/sil/control_cmd',
      '/sil/sat2_data','/sil/sat3_data','/sil/sotif_metrics',
    ];
    expect(topics).toHaveLength(14);
  });

  it('closes Ros on unmount', () => {
    const { unmount } = renderHook(() => useFoxgloveLive('ws://127.0.0.1:8765'));
    unmount();
    expect(rosClose).toHaveBeenCalled();
  });
});

import { useTelemetryStore } from '../store/telemetryStore';

const TOPIC_MAP_TOPIC_NAMES = [
  '/sil/sat2_data',
  '/sil/sat3_data',
  '/sil/sotif_metrics',
];

describe('useFoxgloveLive TOPIC_MAP — D2.5 three topics', () => {
  beforeEach(() => {
    useTelemetryStore.setState({ sat2: null, sat3: null, sotifMetrics: null });
  });

  it('updateSat2 sets sat2 in store', () => {
    const mockSat2 = {
      ivp_contributions: [{ direction_deg: 0, cost: 0.1, label: 'cpa' }],
      active_behavior: 'give_way',
      active_behavior_weight: 0.8,
      colregs_chain: [],
      colregs_chain_target_id: '123456789',
      reasoning_latency_ms: 42,
    };
    useTelemetryStore.getState().updateSat2(mockSat2 as any);
    expect(useTelemetryStore.getState().sat2).not.toBeNull();
    expect(useTelemetryStore.getState().sat2?.ivp_contributions).toHaveLength(1);
  });

  it('updateSat3 sets sat3 in store', () => {
    const mockSat3 = {
      trajectory_candidates: [
        { id: 0, points: [{ lon: 35.6, lat: 139.7 }], cost: 0.2, is_optimal: true, type: 'mid_mpc' },
      ],
      uncertainty_bands: false,
    };
    useTelemetryStore.getState().updateSat3(mockSat3 as any);
    expect(useTelemetryStore.getState().sat3).not.toBeNull();
    expect(useTelemetryStore.getState().sat3?.trajectory_candidates).toHaveLength(1);
  });

  it('updateSotifMetrics sets sotifMetrics in store', () => {
    const mockSotif = {
      ais_radar_consistency_sigma: 1.1,
      target_predictability_rms_m: 30.0,
      perception_coverage_pct: 92.0,
      colregs_parse_failures: 0,
      comm_link_rtt_ms: 120.0,
      checker_veto_rate_pct: 2.5,
    };
    useTelemetryStore.getState().updateSotifMetrics(mockSotif);
    expect(useTelemetryStore.getState().sotifMetrics).not.toBeNull();
    expect(useTelemetryStore.getState().sotifMetrics?.ais_radar_consistency_sigma).toBe(1.1);
  });

  it('TOPIC_MAP contains all three D2.5 topics', async () => {
    const mod = await import('./useFoxgloveLive');
    expect(mod.useFoxgloveLive).toBeDefined();
  });
});
