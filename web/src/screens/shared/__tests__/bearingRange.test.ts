import { describe, it, expect } from 'vitest';
import { computeBearing, computeCpaTcpa, computeRangeNm } from '../navMath';

describe('computeBearing', () => {
  it('north — target directly north of own-ship', () => {
    const b = computeBearing(63.435, 10.395, 63.458, 10.395);
    expect(b).toBeCloseTo(0, 0);
  });

  it('south — target directly south', () => {
    const b = computeBearing(63.458, 10.395, 63.435, 10.395);
    expect(b).toBeCloseTo(180, 0);
  });

  it('east — target at same latitude, east', () => {
    const b = computeBearing(63.435, 10.395, 63.435, 10.500);
    expect(b).toBeGreaterThan(85);
    expect(b).toBeLessThan(95);
  });

  it('west — target at same latitude, west', () => {
    const b = computeBearing(63.435, 10.500, 63.435, 10.395);
    expect(b).toBeGreaterThan(265);
    expect(b).toBeLessThan(275);
  });

  it('same point → bearing 0', () => {
    const b = computeBearing(63.435, 10.395, 63.435, 10.395);
    expect(b).toBe(0);
  });
});

describe('computeRangeNm', () => {
  it('1 nautical mile apart — ~1 minute of latitude', () => {
    const r = computeRangeNm(63.435, 10.395, 63.435 + 1 / 60, 10.395);
    expect(r).toBeCloseTo(1.0, 1);
  });

  it('same point → 0', () => {
    const r = computeRangeNm(63.435, 10.395, 63.435, 10.395);
    expect(r).toBe(0);
  });

  it('~2.5 nm north (R14 head-on scenario)', () => {
    // 2.5 minutes of latitude ≈ 2.5 nm
    const r = computeRangeNm(63.435, 10.395, 63.435 + 2.5 / 60, 10.395);
    expect(r).toBeGreaterThan(2.2);
    expect(r).toBeLessThan(2.8);
  });
});

describe('computeCpaTcpa', () => {
  it('computes closing CPA/TCPA from relative motion when backend metrics are absent', () => {
    const metrics = computeCpaTcpa({
      own: {
        lat: 63.4,
        lon: 10.4,
        sogMps: 5.0,
        cogRad: 0,
      },
      target: {
        lat: 63.43,
        lon: 10.4,
        sogMps: 6.0,
        cogRad: Math.PI,
      },
    });

    expect(metrics).not.toBeNull();
    expect(metrics!.tcpaS).toBeGreaterThan(0);
    expect(metrics!.cpaM).toBeLessThan(50);
    expect(metrics!.cpaPointNM.y).toBeLessThan(1.8);
  });
});
