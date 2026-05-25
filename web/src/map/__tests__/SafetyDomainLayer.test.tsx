import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { render, cleanup } from '@testing-library/react';
import { useRef } from 'react';
import { SafetyDomainLayer } from '../SafetyDomainLayer';
import type { OwnShipState } from '../../types';

let mapContainer: HTMLDivElement;
let layerExists = false;
let sourceExists = false;

const onMock = vi.fn();
const offMock = vi.fn();
const removeLayerMock = vi.fn((layerId: string) => {
  if (layerId.startsWith('safety-')) layerExists = false;
});
const removeSourceMock = vi.fn((sourceId: string) => {
  if (sourceId === 'safety-domain') sourceExists = false;
});

const mockMap = {
  on: onMock,
  off: offMock,
  getContainer: vi.fn(() => mapContainer),
  project: vi.fn(([lon, lat]: [number, number]) => ({
    x: (lon - 10) * 1000,
    y: (64 - lat) * 1000,
  })),
  getLayer: vi.fn((layerId: string) => (layerExists && layerId.startsWith('safety-') ? {} : null)),
  getSource: vi.fn((sourceId: string) => (sourceExists && sourceId === 'safety-domain' ? {} : null)),
  removeLayer: removeLayerMock,
  removeSource: removeSourceMock,
};

function makeOwnShip(lon: number, lat: number): OwnShipState {
  return {
    pose: { lon, lat, heading: 0 },
    kinematics: { sog: 0, cog: 0, rot: 0 },
  } as any;
}

function Wrapper({ ownShip, visible = true }: { ownShip: OwnShipState | null; visible?: boolean }) {
  const ref = useRef(mockMap as any);
  return <SafetyDomainLayer mapRef={ref} ownShip={ownShip} visible={visible} />;
}

beforeEach(() => {
  mapContainer = document.createElement('div');
  Object.defineProperty(mapContainer, 'clientWidth', { configurable: true, value: 1200 });
  Object.defineProperty(mapContainer, 'clientHeight', { configurable: true, value: 800 });
  document.body.appendChild(mapContainer);
  layerExists = false;
  sourceExists = false;
  vi.clearAllMocks();
});

afterEach(() => {
  cleanup();
  mapContainer.remove();
});

describe('SafetyDomainLayer', () => {
  it('renders null when ownShip is missing', () => {
    render(<Wrapper ownShip={null} />);

    expect(mapContainer.querySelector('[data-testid="safety-domain-svg"]')).toBeNull();
  });

  it('renders safety domains as a DOM SVG overlay instead of MapLibre layers', () => {
    render(<Wrapper ownShip={makeOwnShip(10.4, 63.4)} />);

    const svg = mapContainer.querySelector('[data-testid="safety-domain-svg"]');
    expect(svg).not.toBeNull();
    expect(svg?.querySelectorAll('polygon').length).toBe(7);
    expect(svg?.querySelectorAll('polyline').length).toBe(3);
    expect(svg?.querySelectorAll('text').length).toBe(4);
  });

  it('removes stale MapLibre safety layers that could be covered by raster tiles', () => {
    layerExists = true;
    sourceExists = true;

    render(<Wrapper ownShip={makeOwnShip(10.4, 63.4)} />);

    expect(removeLayerMock).toHaveBeenCalled();
    expect(removeSourceMock).toHaveBeenCalledWith('safety-domain');
  });

  it('reprojects overlay geometry on ownShip changes', () => {
    const { rerender } = render(<Wrapper ownShip={makeOwnShip(10.4, 63.4)} />);
    const firstPoints = mapContainer.querySelector('polygon')?.getAttribute('points');

    rerender(<Wrapper ownShip={makeOwnShip(10.5, 63.5)} />);

    expect(mapContainer.querySelector('polygon')?.getAttribute('points')).not.toEqual(firstPoints);
  });
});
