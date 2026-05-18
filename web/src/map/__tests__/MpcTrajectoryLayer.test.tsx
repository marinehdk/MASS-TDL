import { describe, it, expect, vi } from 'vitest';
import { render } from '@testing-library/react';
import { useRef } from 'react';
import { MpcTrajectoryLayer } from '../MpcTrajectoryLayer';
import type { TrajectoryCandidate } from '../../types/sat';

const addSourceMock = vi.fn();
const addLayerMock  = vi.fn();
const setDataMock   = vi.fn();

const mockMap = {
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  addSource: addSourceMock,
  addLayer: addLayerMock,
  getSource: vi.fn(() => ({ setData: setDataMock })),
};

vi.mock('maplibre-gl', () => ({ default: { Map: vi.fn(() => mockMap) } }));

function makeCandidate(id: number, isOptimal: boolean, cost: number, type: 'mid_mpc' | 'bc_mpc'): TrajectoryCandidate {
  return {
    id, cost, is_optimal: isOptimal, type,
    points: [{ lon: 10.4, lat: 63.4 }, { lon: 10.42, lat: 63.42 }],
  };
}

function Wrapper({ candidates }: { candidates: TrajectoryCandidate[] }) {
  const ref = useRef(mockMap as any);
  return <MpcTrajectoryLayer mapRef={ref} candidates={candidates} visible={true} />;
}

describe('MpcTrajectoryLayer', () => {
  it('renders null (no DOM output)', () => {
    const { container } = render(<Wrapper candidates={[]} />);
    expect(container.firstChild).toBeNull();
  });

  it('adds source when candidates provided', () => {
    addSourceMock.mockClear();
    render(<Wrapper candidates={[makeCandidate(0, true, 0.1, 'mid_mpc')]} />);
    expect(addSourceMock).toHaveBeenCalledWith('mpc-trajectories', expect.objectContaining({ type: 'geojson' }));
  });

  it('builds LineString features from candidates', () => {
    addSourceMock.mockClear();
    render(<Wrapper candidates={[
      makeCandidate(0, true, 0.1, 'mid_mpc'),
      makeCandidate(1, false, 0.8, 'bc_mpc'),
    ]} />);
    const callArgs = addSourceMock.mock.calls[0][1].data;
    expect(callArgs.features).toHaveLength(2);
    expect(callArgs.features[0].geometry.type).toBe('LineString');
    expect(callArgs.features[0].properties.cost).toBe(0.1);
  });

  it('updates source on new candidates', () => {
    setDataMock.mockClear();
    addSourceMock.mockClear();
    addLayerMock.mockClear();
    const { rerender } = render(<Wrapper candidates={[makeCandidate(0, true, 0.1, 'mid_mpc')]} />);
    rerender(<Wrapper candidates={[makeCandidate(0, false, 0.5, 'bc_mpc')]} />);
    expect(setDataMock).toHaveBeenCalled();
  });
});
