import { describe, it, expect, vi } from 'vitest';
import { render } from '@testing-library/react';
import { useRef } from 'react';
import { SafetyDomainLayer } from '../SafetyDomainLayer';
import type { OwnShipState } from '../../types';

const addSourceMock = vi.fn();
const addLayerMock  = vi.fn();
const getSourceMock = vi.fn(() => ({ setData: vi.fn() }));

const mockMap = {
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  addSource: addSourceMock,
  addLayer: addLayerMock,
  getSource: getSourceMock,
  removeLayer: vi.fn(),
  removeSource: vi.fn(),
};

vi.mock('maplibre-gl', () => ({
  default: { Map: vi.fn(() => mockMap) },
}));

function makeOwnShip(lon: number, lat: number): OwnShipState {
  return {
    pose: { lon, lat, heading: 0 },
    kinematics: { sog: 0, cog: 0, rot: 0 },
  } as any;
}

function Wrapper({ ownShip }: { ownShip: OwnShipState | null }) {
  const ref = useRef(mockMap as any);
  return <SafetyDomainLayer mapRef={ref} ownShip={ownShip} visible={true} />;
}

describe('SafetyDomainLayer', () => {
  it('renders null (map overlay, no DOM output)', () => {
    const { container } = render(<Wrapper ownShip={null} />);
    expect(container.firstChild).toBeNull();
  });

  it('adds source and layers when ownShip is provided', () => {
    addSourceMock.mockClear();
    addLayerMock.mockClear();
    render(<Wrapper ownShip={makeOwnShip(10.4, 63.4)} />);
    expect(addSourceMock).toHaveBeenCalledWith('safety-domain', expect.objectContaining({ type: 'geojson' }));
    expect(addLayerMock).toHaveBeenCalledTimes(3);
  });

  it('updates source data on ownShip change', () => {
    const setDataMock = vi.fn();
    getSourceMock.mockReturnValue({ setData: setDataMock });
    addSourceMock.mockClear();
    addLayerMock.mockClear();
    const { rerender } = render(<Wrapper ownShip={makeOwnShip(10.4, 63.4)} />);
    rerender(<Wrapper ownShip={makeOwnShip(10.5, 63.5)} />);
    expect(setDataMock).toHaveBeenCalled();
  });
});
