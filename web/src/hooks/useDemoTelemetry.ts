import { useEffect, useRef } from 'react';
import { useTelemetryStore } from '../store';
import { TargetVesselState_ShipType, TargetVesselState_TargetMode } from '../types/sil/target_vessel_state';

const POLL_INTERVAL_MS = 500;

interface DemoTarget {
  mmsi: number;
  lat: number;
  lon: number;
  heading: number;
  sog: number;
  cog: number;
  rot: number;
  ship_type: string;
  mode: string;
}

interface DemoOwnShip {
  lat: number;
  lon: number;
  heading: number;
  sog: number;
  cog: number;
  rot: number;
  u: number;
  v: number;
  r: number;
  rudder_angle: number;
  throttle: number;
}

interface DemoTelemetryResponse {
  error?: string;
  own_ship?: DemoOwnShip;
  targets?: DemoTarget[];
  sim_time?: number;
}

const SHIP_TYPE_MAP: Record<string, TargetVesselState_ShipType> = {
  Unspecified: TargetVesselState_ShipType.UNSPECIFIED,
  Cargo: TargetVesselState_ShipType.CARGO,
  Tanker: TargetVesselState_ShipType.TANKER,
  Passenger: TargetVesselState_ShipType.PASSENGER,
  Fishing: TargetVesselState_ShipType.FISHING,
  Tug: TargetVesselState_ShipType.TUG,
  Pleasure: TargetVesselState_ShipType.PLEASURE,
};

const MODE_MAP: Record<string, TargetVesselState_TargetMode> = {
  unspecified: TargetVesselState_TargetMode.UNSPECIFIED,
  replay: TargetVesselState_TargetMode.REPLAY,
  ncdm: TargetVesselState_TargetMode.NCDM,
  intelligent: TargetVesselState_TargetMode.INTELLIGENT,
};

export function useDemoTelemetry(enabled: boolean) {
  const pollRef = useRef<ReturnType<typeof setInterval>>();
  const deadRef = useRef(false);

  useEffect(() => {
    deadRef.current = false;

    if (!enabled) {
      return;
    }

    const poll = async () => {
      if (deadRef.current) return;
      try {
        const resp = await fetch('/api/v1/demo/telemetry');
        const data: DemoTelemetryResponse = await resp.json();
        if (data.error) return;
        const store = useTelemetryStore.getState();

        if (data.own_ship) {
          const os = data.own_ship;
          store.updateOwnShip({
            pose: { lat: os.lat, lon: os.lon, heading: os.heading },
            kinematics: { sog: os.sog, cog: os.cog, rot: os.rot, u: os.u, v: os.v, r: os.r },
            controlState: { rudderAngle: os.rudder_angle, throttle: os.throttle },
          });
        }

        if (data.targets) {
          store.updateTargets(
            data.targets.map((t) => ({
              mmsi: t.mmsi,
              pose: { lat: t.lat, lon: t.lon, heading: t.heading },
              kinematics: { sog: t.sog, cog: t.cog, rot: t.rot },
              shipType: SHIP_TYPE_MAP[t.ship_type] ?? TargetVesselState_ShipType.UNSPECIFIED,
              mode: MODE_MAP[t.mode] ?? TargetVesselState_TargetMode.UNSPECIFIED,
            }))
          );
        }

        if (typeof data.sim_time === 'number') {
          store.updateLifecycleStatus({ sim_time: data.sim_time });
        }
      } catch {
        // Silently ignore polling errors — WS or demo fallback will retry
      }
    };

    poll(); // Immediate first fetch
    pollRef.current = setInterval(poll, POLL_INTERVAL_MS);

    return () => {
      deadRef.current = true;
      if (pollRef.current) clearInterval(pollRef.current);
    };
  }, [enabled]);
}
