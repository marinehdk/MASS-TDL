/**
 * Compute true bearing (degrees 0-360) from origin to target.
 * NORTAUND convention: 0° = true north, clockwise.
 */
export function computeBearing(
  lat1: number, lon1: number,
  lat2: number, lon2: number,
): number {
  const φ1 = (lat1 * Math.PI) / 180;
  const φ2 = (lat2 * Math.PI) / 180;
  const Δλ = ((lon2 - lon1) * Math.PI) / 180;

  const y = Math.sin(Δλ) * Math.cos(φ2);
  const x = Math.cos(φ1) * Math.sin(φ2) - Math.sin(φ1) * Math.cos(φ2) * Math.cos(Δλ);
  const θ = Math.atan2(y, x);
  return ((θ * 180) / Math.PI + 360) % 360;
}

/**
 * Haversine range in nautical miles.
 *
 * Uses mean Earth radius (3440.065 NM). Accuracy: ≤0.5% error vs WGS-84
 * ellipsoid at mid-latitudes. For DEMO-1 display use this is sufficient;
 * precision navigation should use GeographicLib or similar.
 */
export function computeRangeNm(
  lat1: number, lon1: number,
  lat2: number, lon2: number,
): number {
  const R = 3440.065; // Earth mean radius in nautical miles
  const φ1 = (lat1 * Math.PI) / 180;
  const φ2 = (lat2 * Math.PI) / 180;
  const Δφ = ((lat2 - lat1) * Math.PI) / 180;
  const Δλ = ((lon2 - lon1) * Math.PI) / 180;

  const a = Math.sin(Δφ / 2) ** 2 + Math.cos(φ1) * Math.cos(φ2) * Math.sin(Δλ / 2) ** 2;
  return 2 * R * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

export interface CpaTcpaInput {
  own: {
    lat: number;
    lon: number;
    sogMps?: number;
    cogRad?: number;
  };
  target: {
    lat: number;
    lon: number;
    sogMps?: number;
    cogRad?: number;
  };
}

export interface CpaTcpaMetrics {
  cpaM: number;
  tcpaS: number;
  cpaPointNM: {
    x: number;
    y: number;
  };
}

export function computeCpaTcpa(input: CpaTcpaInput): CpaTcpaMetrics | null {
  const { own, target } = input;
  if (![own.lat, own.lon, target.lat, target.lon].every(Number.isFinite)) return null;

  const metersPerDegLat = 111_320.0;
  const metersPerDegLon = metersPerDegLat * Math.cos((own.lat * Math.PI) / 180);
  const dxM = (target.lon - own.lon) * metersPerDegLon;
  const dyM = (target.lat - own.lat) * metersPerDegLat;

  const ownSog = Number.isFinite(own.sogMps) ? own.sogMps ?? 0 : 0;
  const targetSog = Number.isFinite(target.sogMps) ? target.sogMps ?? 0 : 0;
  const ownCog = Number.isFinite(own.cogRad) ? own.cogRad ?? 0 : 0;
  const targetCog = Number.isFinite(target.cogRad) ? target.cogRad ?? 0 : 0;
  const relVxMps = targetSog * Math.sin(targetCog) - ownSog * Math.sin(ownCog);
  const relVyMps = targetSog * Math.cos(targetCog) - ownSog * Math.cos(ownCog);
  const relSpeed2 = relVxMps * relVxMps + relVyMps * relVyMps;

  const tcpaS = relSpeed2 > 1e-9
    ? Math.max(0, -((dxM * relVxMps) + (dyM * relVyMps)) / relSpeed2)
    : 0;
  const cpaXM = dxM + relVxMps * tcpaS;
  const cpaYM = dyM + relVyMps * tcpaS;

  return {
    cpaM: Math.hypot(cpaXM, cpaYM),
    tcpaS,
    cpaPointNM: {
      x: cpaXM / 1852.0,
      y: cpaYM / 1852.0,
    },
  };
}
