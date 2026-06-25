import { useEffect, useRef, useCallback } from 'react';
import { FoxgloveClient } from '@foxglove/ws-protocol';
import { Ros, Topic } from '@tier4/roslibjs-foxglove';
import { useTelemetryStore, useFsmStore, FSM_STATE_MAP } from '../store';

// foxglove_bridge 3.3.0 (ROS2 Humble, foxglove-sdk-cpp v0.23.0) uses
// foxglove.sdk.v1 subprotocol. @foxglove/ws-protocol hardcodes
// foxglove.websocket.v1. Patch before any connection is made.
(FoxgloveClient as unknown as { SUPPORTED_SUBPROTOCOL: string }).SUPPORTED_SUBPROTOCOL = 'foxglove.sdk.v1';

function degToRad(deg: unknown): number | undefined {
  return typeof deg === 'number' && Number.isFinite(deg) ? deg * Math.PI / 180 : undefined;
}

function knotsToMps(kn: unknown): number | undefined {
  return typeof kn === 'number' && Number.isFinite(kn) ? kn / 1.944 : undefined;
}

// Topic subscription map: topic → handler
//
// IMPORTANT: foxglove_bridge sends ROS2 CDR-decoded messages with the same
// field structure as the .msg file (flat). The TypeScript types are generated
// from proto files which use a nested structure (pose.lat/lon/heading,
// kinematics.sog/cog/rot, etc.). The handlers below adapt flat → nested.
const TOPIC_MAP: Array<{
  topic: string;
  messageType: string;
  handler: (store: ReturnType<typeof useTelemetryStore.getState>, msg: any) => void;
}> = [
  {
    topic: '/sil/own_ship_state',
    messageType: 'sil_msgs/OwnShipState',
    // ROS2 msg: flat {lat, lon, heading, sog, cog, rot, u, v, r, rudder_angle, throttle}
    // TS type:  nested {pose:{lat,lon,heading}, kinematics:{sog,cog,rot,u,v,r}, controlState:{rudderAngle,throttle}}
    handler: (s, msg: any) => {
      s.updateOwnShip({
        pose: { lat: msg.lat, lon: msg.lon, heading: msg.heading },
        kinematics: { sog: msg.sog, cog: msg.cog, rot: msg.rot, u: msg.u, v: msg.v, r: msg.r },
        controlState: { rudderAngle: msg.rudder_angle, throttle: msg.throttle },
      });
    },
  },
  {
    topic: '/sil/target_vessel_state',
    messageType: 'sil_msgs/TargetVesselState',
    // ROS2 msg: flat {mmsi, lat, lon, heading, sog, cog, rot, ship_type, mode}
    // TS type:  nested {mmsi, pose:{lat,lon,heading}, kinematics:{sog,cog,rot}, shipType, mode}
    handler: (s, msg: any) => {
      const adapt = (m: any) => ({
        mmsi: m.mmsi,
        pose: { lat: m.lat, lon: m.lon, heading: m.heading },
        kinematics: { sog: m.sog, cog: m.cog, rot: m.rot },
        shipType: m.ship_type,
        mode: m.mode,
        cpaM: m.cpa_m ?? m.cpaM,
        tcpaS: m.tcpa_s ?? m.tcpaS,
        cpaCovarianceM2: m.cpa_covariance_m2 ?? m.cpaCovarianceM2,
        tcpaCovarianceS2: m.tcpa_covariance_s2 ?? m.tcpaCovarianceS2,
      });
      s.updateTargets(Array.isArray(msg) ? msg.map(adapt) : [adapt(msg)]);
    },
  },
  {
    topic: '/l3/m2/world_state',
    messageType: 'l3_msgs/WorldState',
    handler: (s, msg: any) => {
      const targets = Array.isArray(msg?.targets) ? msg.targets : [];
      s.updateTargets(targets.map((target: any) => ({
        mmsi: Number(target.target_id ?? target.targetId),
        pose: {
          lat: target.position?.latitude,
          lon: target.position?.longitude,
          heading: degToRad(target.heading_deg ?? target.headingDeg),
        },
        kinematics: {
          sog: knotsToMps(target.sog_kn ?? target.sogKn),
          cog: degToRad(target.cog_deg ?? target.cogDeg),
          rot: 0,
        },
        cpaM: target.cpa_m ?? target.cpaM,
        tcpaS: target.tcpa_s ?? target.tcpaS,
        cpaCovarianceM2: target.cpa_covariance_m2 ?? target.cpaCovarianceM2,
        tcpaCovarianceS2: target.tcpa_covariance_s2 ?? target.tcpaCovarianceS2,
        brgDeg: target.brg_deg ?? target.brgDeg,
        rngM: target.rng_m ?? target.rngM,
        encounter: target.encounter,
        confidence: target.confidence,
        rationale: target.rationale,
        sourceSensor: target.source_sensor ?? target.sourceSensor,
      })));
    },
  },
  {
    topic: '/sil/environment',
    messageType: 'sil_msgs/EnvironmentState',
    handler: (s, msg) => s.updateEnvironment(msg),
  },
  {
    topic: '/l3/m1/odd_state',
    messageType: 'l3_msgs/ODDState',
    handler: (s, msg: any) => s.updateOddState(msg),
  },
  {
    topic: '/l3/m4/behavior_plan',
    messageType: 'l3_msgs/BehaviorPlan',
    handler: (s, msg: any) => s.updateBehaviorPlan(msg),
  },
  {
    topic: '/l3/m6/colregs_constraint',
    messageType: 'l3_msgs/COLREGsConstraint',
    handler: (s, msg: any) => s.updateColregsConstraint(msg),
  },
  {
    topic: '/l3/m7/safety_alert',
    messageType: 'l3_msgs/SafetyAlert',
    handler: (s, msg: any) => s.updateSafetyAlert(msg),
  },
  {
    topic: '/l3/m8/threat_state',
    messageType: 'l3_msgs/ThreatState',
    handler: (s, msg: any) => s.updateThreatState(msg),
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
  {
    topic: '/sil/sat2_data',
    messageType: 'l3_msgs/SAT2Data',
    handler: (s, msg: any) => s.updateSat2(msg),
  },
  {
    topic: '/sil/sat3_data',
    messageType: 'l3_msgs/SAT3Data',
    handler: (s, msg: any) => s.updateSat3(msg),
  },
  {
    topic: '/sil/sotif_metrics',
    messageType: 'l3_msgs/SotifMetrics',
    handler: (s, msg: any) => s.updateSotifMetrics(msg),
  },
  {
    topic: '/l2/planned_route',
    messageType: 'l3_external_msgs/PlannedRoute',
    handler: (s, msg: any) => {
      const poses = msg.route?.poses || [];
      const speedProfileKn = Array.isArray(msg.speed_profile_kn)
        ? msg.speed_profile_kn
        : Array.isArray(msg.speedProfileKn)
          ? msg.speedProfileKn
          : [];
      const totalDistanceNm = msg.total_distance_nm ?? msg.totalDistanceNm;
      const estimatedDurationS = msg.estimated_duration_s ?? msg.estimatedDurationS;
      const speedFromDuration = typeof totalDistanceNm === 'number'
        && Number.isFinite(totalDistanceNm)
        && typeof estimatedDurationS === 'number'
        && Number.isFinite(estimatedDurationS)
        && estimatedDurationS > 0
        ? totalDistanceNm / (estimatedDurationS / 3600.0)
        : null;
      const effectiveSpeedProfileKn = speedProfileKn.length > 0
        ? speedProfileKn
        : speedFromDuration !== null
          ? [speedFromDuration]
          : [];
      const waypoints = poses.map((p: any) => ({
        lat: p.pose?.position?.latitude ?? p.pose?.position?.lat ?? 0.0,
        lon: p.pose?.position?.longitude ?? p.pose?.position?.lon ?? 0.0,
      }));
      const cruiseSpeed = effectiveSpeedProfileKn.length > 0
        ? effectiveSpeedProfileKn[0]
        : 10.0;
      s.updateVoyagePlan({
        waypoints,
        cruiseSpeed,
        speedProfileKn: effectiveSpeedProfileKn,
        totalDistanceNm,
        estimatedDurationS,
        routeId: msg.route_id ?? msg.routeId,
        source: 'l2_realtime',
      });
    },
  },
  {
    topic: '/l3/m5/avoidance_plan',
    messageType: 'l3_msgs/AvoidancePlan',
    handler: (s, msg: any) => s.updateAvoidancePlan(msg),
  },
  {
    topic: '/l3/fsm_state',
    messageType: 'l3_msgs/FsmState',
    handler: (_s, msg: any) => {
      const fsmState = FSM_STATE_MAP[msg.current_state] ?? 'TRANSIT';
      useFsmStore.getState()._updateState(
        fsmState,
        msg.active_rule || 'N/A',
        msg.confidence ?? 0.5,
        Date.now() / 1000,
      );
    },
  },
];

// Reconnect config
const BASE_DELAY_MS = 1_000;
const MAX_DELAY_MS = 30_000;

export function useFoxgloveLive(wsUrl = 'wss://127.0.0.1:8765', enabled = true) {
  const rosRef = useRef<Ros | null>(null);
  const topicsRef = useRef<Array<{ unsubscribe: () => void }>>([]);
  const delayRef = useRef(BASE_DELAY_MS);
  const deadRef = useRef(false);
  const reconnectTimerRef = useRef<ReturnType<typeof setTimeout>>();

  // High-frequency telemetry buffers to prevent React rendering congestion
  const latestOwnShipRef = useRef<any>(null);
  const latestTargetsRef = useRef<Map<string, any>>(new Map());

  const setWsConnected = useTelemetryStore((s) => s.setWsConnected);

  // Batched flush loop running at 25 Hz (every 40ms) wall-clock time
  useEffect(() => {
    const interval = setInterval(() => {
      const store = useTelemetryStore.getState();

      if (latestOwnShipRef.current !== null) {
        store.updateOwnShip(latestOwnShipRef.current);
        latestOwnShipRef.current = null;
      }

      if (latestTargetsRef.current.size > 0) {
        const targetsArray = Array.from(latestTargetsRef.current.values());
        store.updateTargets(targetsArray);
        latestTargetsRef.current.clear();
      }
    }, 40); // 25 Hz limit

    return () => clearInterval(interval);
  }, []);

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
          if (topic === '/sil/own_ship_state') {
            const ownMsg = msg as any;
            latestOwnShipRef.current = {
              pose: { lat: ownMsg.lat, lon: ownMsg.lon, heading: ownMsg.heading },
              kinematics: { sog: ownMsg.sog, cog: ownMsg.cog, rot: ownMsg.rot, u: ownMsg.u, v: ownMsg.v, r: ownMsg.r },
              controlState: { rudderAngle: ownMsg.rudder_angle, throttle: ownMsg.throttle },
            };
          } else if (topic === '/sil/target_vessel_state') {
            const targetMsg = msg as any;
            const adapt = (m: any) => ({
              mmsi: m.mmsi,
              pose: { lat: m.lat, lon: m.lon, heading: m.heading },
              kinematics: { sog: m.sog, cog: m.cog, rot: m.rot },
              shipType: m.ship_type,
              mode: m.mode,
              cpaM: m.cpa_m ?? m.cpaM,
              tcpaS: m.tcpa_s ?? m.tcpaS,
              cpaCovarianceM2: m.cpa_covariance_m2 ?? m.cpaCovarianceM2,
              tcpaCovarianceS2: m.tcpa_covariance_s2 ?? m.tcpaCovarianceS2,
            });
            const adapted = Array.isArray(targetMsg) ? targetMsg.map(adapt) : [adapt(targetMsg)];
            
            for (const t of adapted) {
              latestTargetsRef.current.set(String(t.mmsi), t);
            }
          } else {
            handler(store, msg);
          }
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

    if (!enabled) {
      setWsConnected(false);
      return;
    }

    function connect() {
      if (deadRef.current) return;
      console.log('[Foxglove] connecting to', wsUrl);

      const ros = new Ros({ url: wsUrl });
      rosRef.current = ros;

      ros.on('connection', () => {
        console.log('[Foxglove] connected');
        delayRef.current = BASE_DELAY_MS;
        setWsConnected(true);
        // Subscribe AFTER connection is established so foxglove_bridge
        // can match requests against its channel advertisement list.
        subscribeAll(ros);
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

      // subscribeAll is now called inside ros.on('connection') above.
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
  }, [wsUrl, enabled]);

  return rosRef;
}
