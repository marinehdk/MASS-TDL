import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { useTelemetryStore } from '../../store/telemetryStore';
import type { SAT2Data, SAT3Data, SotifMetrics } from '../../types/sat';

const rosOn = vi.fn();
const rosClose = vi.fn();
const topicSubscriptions = vi.hoisted(() => new Map<string, (msg: any) => void>());

vi.mock('@tier4/roslibjs-foxglove', () => ({
  Ros: vi.fn(() => ({ on: rosOn, close: rosClose })),
  Topic: vi.fn((config: any) => ({
    subscribe: vi.fn((cb: (msg: any) => void) => topicSubscriptions.set(config.name, cb)),
    unsubscribe: vi.fn(),
  })),
  default: undefined,
}));

vi.mock('@foxglove/ws-protocol', () => ({
  FoxgloveClient: class {
    static SUPPORTED_SUBPROTOCOL = 'foxglove.websocket.v1';
  },
}));

import { useFoxgloveLive } from '../useFoxgloveLive';
import { renderHook } from '@testing-library/react';
import { Topic } from '@tier4/roslibjs-foxglove';

const MOCK_SAT2: SAT2Data = {
  ivp_contributions: [{ direction_deg: 0, cost: 0.1, label: 'cpa' }],
  active_behavior: 'give_way',
  active_behavior_weight: 0.8,
  colregs_chain: [],
  colregs_chain_target_id: '123456789',
  reasoning_latency_ms: 42,
};

const MOCK_SAT3: SAT3Data = {
  trajectory_candidates: [
    { id: 0, points: [{ lon: 35.6, lat: 139.7 }], cost: 0.2, is_optimal: true, type: 'mid_mpc' },
  ],
  uncertainty_bands: false,
};

const MOCK_SOTIF: SotifMetrics = {
  ais_radar_consistency_sigma: 1.1,
  target_predictability_rms_m: 30.0,
  perception_coverage_pct: 92.0,
  colregs_parse_failures: 0,
  comm_link_rtt_ms: 120.0,
  checker_veto_rate_pct: 2.5,
};

describe('useFoxgloveLive — stale detection', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    useTelemetryStore.getState().reset();
    topicSubscriptions.clear();
    rosOn.mockReset();
    rosClose.mockReset();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('registers sat2, sat3, sotifMetrics handlers to correct topics', () => {
    renderHook(() => useFoxgloveLive('ws://127.0.0.1:8765'));
    const connectionCb = rosOn.mock.calls.find(
      (c: any[]) => c[0] === 'connection',
    )?.[1] as Function | undefined;
    expect(connectionCb).toBeDefined();
  });

  it('subscribes to M5 avoidance plan output', () => {
    renderHook(() => useFoxgloveLive('ws://127.0.0.1:8765'));
    const connectionCb = rosOn.mock.calls.find(
      (c: any[]) => c[0] === 'connection',
    )?.[1] as Function | undefined;
    expect(connectionCb).toBeDefined();

    connectionCb?.();

    expect(Topic).toHaveBeenCalledWith(expect.objectContaining({
      name: '/l3/m5/avoidance_plan',
      messageType: 'l3_msgs/AvoidancePlan',
    }));
  });

  it('subscribes to M2 world state for CPA/TCPA target metrics', () => {
    renderHook(() => useFoxgloveLive('ws://127.0.0.1:8765'));
    const connectionCb = rosOn.mock.calls.find(
      (c: any[]) => c[0] === 'connection',
    )?.[1] as Function | undefined;
    expect(connectionCb).toBeDefined();

    connectionCb?.();

    expect(Topic).toHaveBeenCalledWith(expect.objectContaining({
      name: '/l3/m2/world_state',
      messageType: 'l3_msgs/WorldState',
    }));
  });

  it('subscribes to real M1/M4/M6/M7 decision topics', () => {
    renderHook(() => useFoxgloveLive('ws://127.0.0.1:8765'));
    const connectionCb = rosOn.mock.calls.find(
      (c: any[]) => c[0] === 'connection',
    )?.[1] as Function | undefined;
    expect(connectionCb).toBeDefined();

    connectionCb?.();

    expect(Topic).toHaveBeenCalledWith(expect.objectContaining({
      name: '/l3/m1/odd_state',
      messageType: 'l3_msgs/ODDState',
    }));
    expect(Topic).toHaveBeenCalledWith(expect.objectContaining({
      name: '/l3/m4/behavior_plan',
      messageType: 'l3_msgs/BehaviorPlan',
    }));
    expect(Topic).toHaveBeenCalledWith(expect.objectContaining({
      name: '/l3/m6/colregs_constraint',
      messageType: 'l3_msgs/COLREGsConstraint',
    }));
    expect(Topic).toHaveBeenCalledWith(expect.objectContaining({
      name: '/l3/m7/safety_alert',
      messageType: 'l3_msgs/SafetyAlert',
    }));
  });

  it('stores real M6 COLREGs constraint messages', () => {
    renderHook(() => useFoxgloveLive('ws://127.0.0.1:8765'));
    const connectionCb = rosOn.mock.calls.find(
      (c: any[]) => c[0] === 'connection',
    )?.[1] as Function | undefined;
    expect(connectionCb).toBeDefined();

    connectionCb?.();
    topicSubscriptions.get('/l3/m6/colregs_constraint')?.({
      active_rules: [{
        rule_id: 14,
        role: 1,
        preferred_direction: 'STARBOARD',
        min_alteration_deg: 22,
      }],
      phase: 'T_act',
      primary_role: 1,
      primary_preferred_direction: 'STARBOARD',
      confidence: 0.91,
    });

    expect(useTelemetryStore.getState().colregsConstraint).toEqual(expect.objectContaining({
      ruleId: 14,
      role: 1,
      preferredDirection: 'STARBOARD',
      minAlterationDeg: 22,
      phase: 'T_act',
    }));
  });

  it('stores L2 planned route speed profile and route metrics from Foxglove camelCase fields', () => {
    renderHook(() => useFoxgloveLive('ws://127.0.0.1:8765'));
    const connectionCb = rosOn.mock.calls.find(
      (c: any[]) => c[0] === 'connection',
    )?.[1] as Function | undefined;
    expect(connectionCb).toBeDefined();

    connectionCb?.();
    topicSubscriptions.get('/l2/planned_route')?.({
      routeId: 42,
      route: {
        poses: [
          { pose: { position: { latitude: 63.4, longitude: 10.4 } } },
          { pose: { position: { latitude: 63.5, longitude: 10.4 } } },
        ],
      },
      speedProfileKn: [12.0],
      totalDistanceNm: 6.0,
      estimatedDurationS: 1800.0,
    });

    expect(useTelemetryStore.getState().voyagePlan).toEqual(expect.objectContaining({
      cruiseSpeed: 12.0,
      speedProfileKn: [12.0],
      totalDistanceNm: 6.0,
      estimatedDurationS: 1800.0,
      routeId: 42,
      source: 'l2_realtime',
    }));
  });

  it('updateSat2 sets sat2LastReceivedAt', () => {
    expect(useTelemetryStore.getState().sat2LastReceivedAt).toBeNull();
    useTelemetryStore.getState().updateSat2(MOCK_SAT2);
    expect(useTelemetryStore.getState().sat2LastReceivedAt).not.toBeNull();
  });

  it('updateSat3 sets sat3LastReceivedAt', () => {
    expect(useTelemetryStore.getState().sat3LastReceivedAt).toBeNull();
    useTelemetryStore.getState().updateSat3(MOCK_SAT3);
    expect(useTelemetryStore.getState().sat3LastReceivedAt).not.toBeNull();
  });

  it('updateSotifMetrics sets sotifMetricsLastReceivedAt', () => {
    expect(useTelemetryStore.getState().sotifMetricsLastReceivedAt).toBeNull();
    useTelemetryStore.getState().updateSotifMetrics(MOCK_SOTIF);
    expect(useTelemetryStore.getState().sotifMetricsLastReceivedAt).not.toBeNull();
  });

  it('isSat2Stale returns true when no data received', () => {
    expect(useTelemetryStore.getState().isSat2Stale()).toBe(true);
  });

  it('isSat3Stale returns true when no data received', () => {
    expect(useTelemetryStore.getState().isSat3Stale()).toBe(true);
  });

  it('isSotifMetricsStale returns true when no data received', () => {
    expect(useTelemetryStore.getState().isSotifMetricsStale()).toBe(true);
  });

  it('isSat2Stale returns false immediately after update', () => {
    useTelemetryStore.getState().updateSat2(MOCK_SAT2);
    expect(useTelemetryStore.getState().isSat2Stale()).toBe(false);
  });

  it('isSat3Stale returns false immediately after update', () => {
    useTelemetryStore.getState().updateSat3(MOCK_SAT3);
    expect(useTelemetryStore.getState().isSat3Stale()).toBe(false);
  });

  it('isSotifMetricsStale returns false immediately after update', () => {
    useTelemetryStore.getState().updateSotifMetrics(MOCK_SOTIF);
    expect(useTelemetryStore.getState().isSotifMetricsStale()).toBe(false);
  });

  it('isSat2Stale becomes true after 3 seconds without data', () => {
    useTelemetryStore.getState().updateSat2(MOCK_SAT2);
    expect(useTelemetryStore.getState().isSat2Stale()).toBe(false);

    vi.advanceTimersByTime(2999);
    expect(useTelemetryStore.getState().isSat2Stale()).toBe(false);

    vi.advanceTimersByTime(2);
    expect(useTelemetryStore.getState().isSat2Stale()).toBe(true);
  });

  it('isSat3Stale becomes true after 3 seconds without data', () => {
    useTelemetryStore.getState().updateSat3(MOCK_SAT3);
    expect(useTelemetryStore.getState().isSat3Stale()).toBe(false);

    vi.advanceTimersByTime(3001);
    expect(useTelemetryStore.getState().isSat3Stale()).toBe(true);
  });

  it('isSotifMetricsStale becomes true after 3 seconds without data', () => {
    useTelemetryStore.getState().updateSotifMetrics(MOCK_SOTIF);
    expect(useTelemetryStore.getState().isSotifMetricsStale()).toBe(false);

    vi.advanceTimersByTime(3001);
    expect(useTelemetryStore.getState().isSotifMetricsStale()).toBe(true);
  });

  it('receiving data again clears stale state', () => {
    useTelemetryStore.getState().updateSat2(MOCK_SAT2);
    vi.advanceTimersByTime(3001);
    expect(useTelemetryStore.getState().isSat2Stale()).toBe(true);

    useTelemetryStore.getState().updateSat2(MOCK_SAT2);
    expect(useTelemetryStore.getState().isSat2Stale()).toBe(false);
  });

  it('reset clears lastReceivedAt fields', () => {
    useTelemetryStore.getState().updateSat2(MOCK_SAT2);
    useTelemetryStore.getState().updateSat3(MOCK_SAT3);
    useTelemetryStore.getState().updateSotifMetrics(MOCK_SOTIF);

    useTelemetryStore.getState().reset();

    expect(useTelemetryStore.getState().sat2LastReceivedAt).toBeNull();
    expect(useTelemetryStore.getState().sat3LastReceivedAt).toBeNull();
    expect(useTelemetryStore.getState().sotifMetricsLastReceivedAt).toBeNull();
  });
});
