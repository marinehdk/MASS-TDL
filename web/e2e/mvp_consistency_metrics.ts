export type HeadingSample = {
  sim_t: number;
  hdg: number;
};

export function angularDeltaFromStartDeg(startDeg: number, headingDeg: number): number {
  let delta = Math.abs(headingDeg - startDeg) % 360;
  if (delta > 180) delta = 360 - delta;
  return delta;
}

export function peakAngularChangeFromStartDeg(headingsDeg: number[]): number {
  if (headingsDeg.length === 0) {
    throw new Error('peakAngularChangeFromStartDeg needs at least one heading');
  }
  const start = headingsDeg[0];
  return Math.max(...headingsDeg.map((heading) => angularDeltaFromStartDeg(start, heading)));
}

export function latestRunSegment(samples: HeadingSample[]): HeadingSample[] {
  if (samples.length === 0) return [];
  let segmentStart = 0;
  for (let i = 1; i < samples.length; i++) {
    if (samples[i].sim_t < samples[i - 1].sim_t) {
      segmentStart = i;
    }
  }
  return samples.slice(segmentStart);
}
