import { useEffect, useRef, useCallback } from 'react';
import { Ros, Topic } from '@tier4/roslibjs-foxglove';
import { useTelemetryStore } from '../store';

// Topic subscription map: topic → handler
const TOPIC_MAP: Array<{
  topic: string;
  messageType: string;
  handler: (store: ReturnType<typeof useTelemetryStore.getState>, msg: any) => void;
}> = [
  {
    topic: '/sil/own_ship_state',
    messageType: 'sil_msgs/OwnShipState',
    handler: (s, msg) => s.updateOwnShip(msg),
  },
  {
    topic: '/sil/target_vessel_state',
    messageType: 'sil_msgs/TargetVesselState',
    handler: (s, msg) => s.updateTargets(Array.isArray(msg) ? msg : [msg]),
  },
  {
    topic: '/sil/environment',
    messageType: 'sil_msgs/EnvironmentState',
    handler: (s, msg) => s.updateEnvironment(msg),
  },
  {
    topic: '/sil/module_pulse',
    messageType: 'sil_msgs/ModulePulse',
    handler: (s, msg) => s.updateModulePulses(Array.isArray(msg) ? msg : [msg]),
  },
  {
    topic: '/sil/asdr_event',
    messageType: 'sil_msgs/ASDREvent',
    handler: (s, msg) => s.appendAsdrEvent(msg),
  },
  {
    topic: '/sil/lifecycle_status',
    messageType: 'sil_msgs/LifecycleStatus',
    handler: (s, msg) => s.updateLifecycleStatus(msg),
  },
  {
    topic: '/sil/scoring',
    messageType: 'sil_msgs/ScoringRow',
    handler: (s, msg) => s.updateScoringRow(msg),
  },
  {
    topic: '/sil/sensor_status',
    messageType: 'sil_msgs/ModulePulse',
    handler: (s, msg) => s.updateSensors(Array.isArray(msg) ? msg : [msg]),
  },
  {
    topic: '/sil/commlink_status',
    messageType: 'sil_msgs/ModulePulse',
    handler: (s, msg) => s.updateCommLinks(Array.isArray(msg) ? msg : [msg]),
  },
  {
    topic: '/sil/fault_status',
    messageType: 'sil_msgs/FaultEvent',
    handler: (s, msg) => s.updateFaultStatus(Array.isArray(msg) ? msg : [msg]),
  },
  {
    topic: '/sil/control_cmd',
    messageType: 'ship_sim_interfaces/ActuatorCmd',
    handler: (s, msg) => s.updateControlCmd(msg),
  },
];

// Reconnect config
const BASE_DELAY_MS = 1_000;
const MAX_DELAY_MS = 30_000;

export function useFoxgloveLive(wsUrl = 'ws://127.0.0.1:8765') {
  const rosRef = useRef<Ros | null>(null);
  const topicsRef = useRef<Array<{ unsubscribe: () => void }>>([]);
  const delayRef = useRef(BASE_DELAY_MS);
  const deadRef = useRef(false);
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout>>();

  const setWsConnected = useTelemetryStore((s) => s.setWsConnected);

  const subscribeAll = useCallback((ros: Ros) => {
    const subs: Array<{ unsubscribe: () => void }> = [];

    for (const { topic, messageType, handler } of TOPIC_MAP) {
      const rosTopic = new Topic({
        ros,
        name: topic,
        messageType,
      });

      rosTopic.subscribe((msg: unknown) => {
        const store = useTelemetryStore.getState();
        try {
          handler(store, msg);
        } catch (err) {
          console.warn(`[Foxglove] Error handling ${topic}:`, err);
        }
      });

      subs.push({ unsubscribe: () => rosTopic.unsubscribe() });
    }

    topicsRef.current = subs;
  }, []);

  useEffect(() => {
    deadRef.current = false;

    function connect() {
      if (deadRef.current) return;
      console.log('[Foxglove] connecting to', wsUrl);

      const ros = new Ros({ url: wsUrl });
      rosRef.current = ros;

      ros.on('connection', () => {
        console.log('[Foxglove] connected');
        delayRef.current = BASE_DELAY_MS;
        setWsConnected(true);
      });

      ros.on('close', () => {
        setWsConnected(false);
        if (deadRef.current) return;
        console.log(`[Foxglove] closed — reconnecting in ${delayRef.current}ms`);
        reconnectTimerRef.current = setTimeout(() => {
          delayRef.current = Math.min(delayRef.current * 2, MAX_DELAY_MS);
          connect();
        }, delayRef.current);
      });

      ros.on('error', (err: Error) => {
        console.warn('[Foxglove] error:', err.message);
      });

      // Subscribe to all topics once connected
      subscribeAll(ros);
    }

    connect();

    return () => {
      deadRef.current = true;
      // Cleanup all topic subscriptions
      for (const sub of topicsRef.current) {
        try { sub.unsubscribe(); } catch {}
      }
      topicsRef.current = [];
      // Close ROS connection
      try { rosRef.current?.close(); } catch {}
      rosRef.current = null;
      // Clear reconnect timer
      if (reconnectTimerRef.current) clearTimeout(reconnectTimerRef.current);
      setWsConnected(false);
    };
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [wsUrl]);

  return rosRef;
}
