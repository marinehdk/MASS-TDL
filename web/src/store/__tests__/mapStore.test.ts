import { describe, it, expect, beforeEach } from 'vitest';
import { useMapStore } from '../mapStore';

describe('mapStore', () => {
  beforeEach(() => {
    useMapStore.getState().resetViewport();
  });

  it('has default Trondheim viewport', () => {
    const v = useMapStore.getState().viewport;
    expect(v.center).toEqual([10.38, 63.44]);
    expect(v.zoom).toBe(12);
    expect(v.bearing).toBe(0);
    expect(v.pitch).toBe(0);
  });

  it('setViewport partial updates', () => {
    useMapStore.getState().setViewport({ zoom: 14, bearing: 45 });
    const v = useMapStore.getState().viewport;
    expect(v.zoom).toBe(14);
    expect(v.bearing).toBe(45);
    expect(v.center).toEqual([10.38, 63.44]); // unchanged
  });

  it('resetViewport restores defaults', () => {
    useMapStore.getState().setViewport({ zoom: 8, center: [0, 0] });
    useMapStore.getState().resetViewport();
    const v = useMapStore.getState().viewport;
    expect(v.zoom).toBe(12);
    expect(v.center).toEqual([10.38, 63.44]);
  });
});
