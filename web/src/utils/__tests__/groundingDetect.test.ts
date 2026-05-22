import { describe, it, expect } from 'vitest';
import { predictedPath, checkGroundingRisk, OwnShipPosition } from '../groundingDetect';

// Isefjord / Trondelag test area
const SHIP_E: OwnShipPosition = { lat: 63.42, lon: 10.38, cog_deg: 90, sog_kn: 12 };
const SHIP_STATIONARY: OwnShipPosition = { lat: 63.42, lon: 10.38, cog_deg: 0, sog_kn: 0.05 };

function polygonBox(minLon: number, minLat: number, maxLon: number, maxLat: number): GeoJSON.Feature {
  return {
    type: 'Feature',
    id: `box-${minLon}-${minLat}`,
    properties: { id: `box-${minLon}-${minLat}` },
    geometry: {
      type: 'Polygon',
      coordinates: [[
        [minLon, minLat],
        [maxLon, minLat],
        [maxLon, maxLat],
        [minLon, maxLat],
        [minLon, minLat],
      ]],
    },
  };
}

describe('groundingDetect', () => {
  describe('predictedPath', () => {
    it('returns a LineString with 13 coordinates (0, 10, 20, … 120 s)', () => {
      const path = predictedPath(SHIP_E);
      expect(path.type).toBe('Feature');
      expect(path.geometry.type).toBe('LineString');
      expect(path.geometry.coordinates).toHaveLength(13);
      // First point is start position
      expect(path.geometry.coordinates[0][0]).toBeCloseTo(SHIP_E.lon, 4);
      expect(path.geometry.coordinates[0][1]).toBeCloseTo(SHIP_E.lat, 4);
      // Last point should be eastward (heading 90°)
      expect(path.geometry.coordinates[12][0]).toBeGreaterThan(SHIP_E.lon);
    });
  });

  describe('checkGroundingRisk', () => {
    it('returns no risk when SOG < 0.1 kn (stationary)', () => {
      const result = checkGroundingRisk(SHIP_STATIONARY, [
        polygonBox(10.35, 63.38, 10.50, 63.50),
      ]);
      expect(result.isGroundingRisk).toBe(false);
      expect(result.riskPolygonIds).toHaveLength(0);
      expect(result.predictionHorizonS).toBe(120);
    });

    it('detects risk when predicted path intersects a polygon ahead', () => {
      // Ship heading east (90°) at 12 kn from 10.38 → path reaches ~10.395°E in 120 s.
      // Place polygon in that narrow window.
      const result = checkGroundingRisk(SHIP_E, [
        polygonBox(10.385, 63.415, 10.392, 63.425),
      ]);
      expect(result.isGroundingRisk).toBe(true);
      expect(result.riskPolygonIds.length).toBeGreaterThanOrEqual(1);
    });

    it('returns no risk when polygon is behind the ship', () => {
      // Polygon west of ship (behind for heading 90° east)
      const result = checkGroundingRisk(SHIP_E, [
        polygonBox(10.30, 63.40, 10.36, 63.44),
      ]);
      expect(result.isGroundingRisk).toBe(false);
      expect(result.riskPolygonIds).toHaveLength(0);
    });
  });
});
