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
