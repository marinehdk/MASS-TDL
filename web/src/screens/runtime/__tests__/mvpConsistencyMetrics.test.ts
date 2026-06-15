import { describe, expect, it } from 'vitest';
import {
  angularDeltaFromStartDeg,
  latestRunSegment,
  peakAngularChangeFromStartDeg,
} from '../../../../e2e/mvp_consistency_metrics';

describe('mvp consistency turn metrics', () => {
  it('does not count zero-degree wrap jitter as an avoidance turn', () => {
    expect(angularDeltaFromStartDeg(0, 359.8)).toBeCloseTo(0.2, 6);
    expect(peakAngularChangeFromStartDeg([0, 359.8, 0.1, 360])).toBeLessThan(1);
  });

  it('counts peak maneuver even after the vessel returns to route heading', () => {
    const headings = [0, 12, 31, 62.7, 45, 8.6, 0.1];

    expect(peakAngularChangeFromStartDeg(headings)).toBeCloseTo(62.7, 6);
  });

  it('uses the latest sim-time segment after restart-between-runs traces', () => {
    const segment = latestRunSegment([
      { sim_t: 34, hdg: 0 },
      { sim_t: 120, hdg: 0.2 },
      { sim_t: 0.1, hdg: 0 },
      { sim_t: 200, hdg: 62.7 },
      { sim_t: 500, hdg: 0.1 },
    ]);

    expect(segment.map((s) => s.hdg)).toEqual([0, 62.7, 0.1]);
    expect(peakAngularChangeFromStartDeg(segment.map((s) => s.hdg))).toBeCloseTo(62.7, 6);
  });
});
