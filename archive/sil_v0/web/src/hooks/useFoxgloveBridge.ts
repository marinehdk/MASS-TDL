import { useEffect, useRef, useState } from 'react';
import type { NavState, TargetState } from '../types/sil';
import { useThrottledTopic } from './useThrottledTopic';

// Dynamic import — roslibjs-foxglove only loaded when called
async function loadRoslib() {
  const ROSLIB = await import('@tier4/roslibjs-foxglove');
  return ROSLIB.default ?? ROSLIB;
}

interface FoxgloveData {
  ownShip: NavState | null;
  targets: TargetState[];
  connected: boolean;
}

/**
 * Connect to foxglove_bridge (port 8765), subscribe to throttled topics.
 * Returns live own-ship + targets data for MapLibre layers.
 */
export function useFoxgloveBridge(): FoxgloveData {
  const [ownShip, setOwnShip] = useState<NavState | null>(null);
  const [targets, setTargets] = useState<TargetState[]>([]);
  const [connected, setConnected] = useState(false);
  const rosRef = useRef<any>(null);

  // Throttle own-ship updates to 15fps
  const updateOwnShip = useThrottledTopic((nav: NavState) => {
    setOwnShip(nav);
  }, 15);

  // Throttle target updates to 5fps
  const updateTargets = useThrottledTopic((tgts: TargetState[]) => {
    setTargets(tgts);
  }, 5);

  useEffect(() => {
    let cancelled = false;

    loadRoslib().then((ROSLIB) => {
      if (cancelled) return;

      const ros = new ROSLIB.Ros({ url: 'ws://localhost:8765' });
      rosRef.current = ros;

      ros.on('connection', () => {
        if (cancelled) return;
        setConnected(true);

        // Subscribe to throttled own-ship nav state
        const navTopic = new ROSLIB.Topic({
          ros,
          name: '/nav/filtered_state_viz',
          messageType: 'l3_external_msgs/msg/FilteredOwnShipState',
        });
        navTopic.subscribe((msg: any) => {
          updateOwnShip({
            lat: msg.position?.latitude ?? msg.latitude ?? msg.lat ?? 0,
            lon: msg.position?.longitude ?? msg.longitude ?? msg.lon ?? 0,
            sog_kn: msg.sog_kn ?? msg.sog ?? 0,
            cog_deg: msg.cog_deg ?? msg.cog ?? 0,
            heading_deg: msg.heading_deg ?? msg.heading ?? 0,
          });
        });

        // Subscribe to throttled tracks
        const tracksTopic = new ROSLIB.Topic({
          ros,
          name: '/world_model/tracks_viz',
          messageType: 'l3_external_msgs/msg/TrackedTargetArray',
        });
        tracksTopic.subscribe((msg: any) => {
          const arr = msg.targets ?? msg.tracks ?? [];
          updateTargets(arr.map((t: any) => {
            // Map encounter is_giveway to role
            let role: 'give-way' | 'stand-on' | 'overtaking' | 'safe' = 'safe';
            if (t.encounter) {
              role = t.encounter.is_giveway ? 'give-way' : 'stand-on';
            }
            
            return {
              mmsi: String(t.target_id ?? t.mmsi ?? t.id ?? ''),
              lat: t.position?.latitude ?? t.latitude ?? t.lat ?? 0,
              lon: t.position?.longitude ?? t.longitude ?? t.lon ?? 0,
              sog_kn: t.sog_kn ?? t.sog ?? 0,
              cog_deg: t.cog_deg ?? t.cog ?? 0,
              heading_deg: t.heading_deg ?? t.heading ?? 0,
              cpa_nm: t.cpa_m ? (t.cpa_m / 1852.0) : (t.cpa_nm ?? t.cpa ?? 999),
              tcpa_s: t.tcpa_s ?? t.tcpa ?? 9999,
              colreg_role: role,
              confidence: t.confidence ?? 1.0,
            };
          }));
        });
      });

      ros.on('error', () => { if (!cancelled) setConnected(false); });
      ros.on('close', () => { if (!cancelled) setConnected(false); });
    });

    return () => {
      cancelled = true;
      if (rosRef.current) {
        try { rosRef.current.close(); } catch { /* ignore */ }
      }
    };
  }, []);

  return { ownShip, targets, connected };
}
