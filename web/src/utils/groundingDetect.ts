// SPDX-License-Identifier: MIT
// Grounding hazard detection via Turf.js — 10Hz dead-reckoning prediction.

import { lineString, booleanIntersects } from '@turf/turf';

/**
 * Simplified own-ship position consumed by grounding detection.
 * cog_deg in degrees true, sog_kn in knots.
 */
export interface OwnShipPosition {
  lat: number;
  lon: number;
  cog_deg: number;
  sog_kn: number;
}

/** Nautical mile → degrees (1 nm ≈ 1/60° at equator). */
const NM_TO_DEG = 1 / 60;

/** Number of 10 s intervals in the 120 s horizon. */
const SEGMENTS = 12;

/** One interval in hours (10 s / 3600). */
const DT_HOURS = 10 / 3600;

/**
 * Project a point by bearing and nautical-mile distance.
 * Reuses the same projection as SilMapView.tsx for consistent geometry.
 */
function project(
  lon: number,
  lat: number,
  bearingDeg: number,
  distNm: number,
): [number, number] {
  const d = distNm * NM_TO_DEG;
  const brRad = bearingDeg * (Math.PI / 180);
  const cosLat = Math.cos(lat * (Math.PI / 180)) || 1e-9;
  return [lon + (d * Math.sin(brRad)) / cosLat, lat + d * Math.cos(brRad)];
}

/**
 * Predicted dead-reckoning path for the next 120 seconds.
 *
 * Returns a Turf.js LineString with **13** waypoints (t = 0, 10, 20, … 120 s).
 * Linear projection only — rudder angle / ROT is intentionally ignored per
 * the D2.5 spec (pure dead reckoning).
 *
 * @returns GeoJSON Feature<LineString> with coordinates [lon, lat][]
 */
export function predictedPath(
  ship: OwnShipPosition,
): GeoJSON.Feature<GeoJSON.LineString> {
  const { lat, lon, cog_deg, sog_kn } = ship;

  const coords: [number, number][] = [[lon, lat]];

  for (let i = 1; i <= SEGMENTS; i++) {
    const distNm = sog_kn * DT_HOURS * i; // cumulative
    coords.push(project(lon, lat, cog_deg, distNm));
  }

  return lineString(coords);
}

export interface GroundingRiskResult {
  isGroundingRisk: boolean;
  riskPolygonIds: string[];
  predictionHorizonS: number;
}

/**
 * Evaluate grounding risk by testing the predicted 120 s path against a set
 * of ENC polygon features (land, depth-area, danger-area, …).
 *
 * **SOG < 0.1 kn** is treated as stationary → no risk regardless of geometry.
 *
 * @param ship        Current own-ship position
 * @param encPolygons Rendered ENC features from MapLibre (fill layers only)
 */
export function checkGroundingRisk(
  ship: OwnShipPosition,
  encPolygons: GeoJSON.Feature[],
): GroundingRiskResult {
  // Stationary → no risk
  if (ship.sog_kn < 0.1) {
    return { isGroundingRisk: false, riskPolygonIds: [], predictionHorizonS: 120 };
  }

  const path = predictedPath(ship);
  const riskPolygonIds: string[] = [];

  for (const poly of encPolygons) {
    // Only area geometries are relevant for grounding
    if (
      poly.geometry.type !== 'Polygon' &&
      poly.geometry.type !== 'MultiPolygon'
    ) {
      continue;
    }

    if (booleanIntersects(path, poly)) {
      const id =
        poly.id != null
          ? String(poly.id)
          : poly.properties?.id != null
            ? String(poly.properties.id)
            : '';
      riskPolygonIds.push(id);
    }
  }

  return {
    isGroundingRisk: riskPolygonIds.length > 0,
    riskPolygonIds,
    predictionHorizonS: 120,
  };
}
