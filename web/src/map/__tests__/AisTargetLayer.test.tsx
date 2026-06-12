import { act, render, screen } from '@testing-library/react';
import { useRef } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { fetchLatestAisTargets, type AisTarget } from '../../api/aisTwinApi';
import { AisTargetLayer, buildAisTargetGeoJSON } from '../AisTargetLayer';

const addSourceMock = vi.fn();
const addLayerMock = vi.fn();
const setDataMock = vi.fn();
const fitBoundsMock = vi.fn();
const onMock = vi.fn();
const offMock = vi.fn();
let eventHandlers: Record<string, (event: any) => void> = {};

const mockMap = {
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  addSource: addSourceMock,
  addLayer: addLayerMock,
  fitBounds: fitBoundsMock,
  getSource: vi.fn(() => ({ setData: setDataMock })),
  on: onMock,
  off: offMock,
  getCanvas: vi.fn(() => ({ style: { cursor: '' } })),
};

vi.mock('maplibre-gl', () => ({ default: { Map: vi.fn(() => mockMap) } }));

const targetOne: AisTarget = {
  target_id: 123456789,
  lat: -2.0,
  lon: 106.0,
  sog_kn: 12,
  cog_deg: 90,
  heading_deg: null,
  source_sensor: 'ais',
  ship_name: 'CHITOSE',
  ship_type: 'cargo',
  destination: 'PELINTUNG',
  vessel_length_m: 180,
  received_at_utc: '2026-06-12T00:00:05+00:00',
};

const targetTwo: AisTarget = {
  target_id: 987654321,
  lat: -2.1,
  lon: 106.1,
  sog_kn: 8,
  cog_deg: 135,
  heading_deg: 140,
  source_sensor: 'ais',
  ship_name: 'SPUBLICINDA',
  ship_type: 'tanker',
  destination: 'BENOA',
  vessel_length_m: 95,
  received_at_utc: '2026-06-12T00:00:02+00:00',
};

function Wrapper({ targets, visible = true }: { targets: AisTarget[]; visible?: boolean }) {
  const ref = useRef(mockMap as any);
  return <AisTargetLayer mapRef={ref} targets={targets} visible={visible} />;
}

describe('AisTargetLayer', () => {
  afterEach(() => {
    vi.restoreAllMocks();
    addSourceMock.mockClear();
    addLayerMock.mockClear();
    setDataMock.mockClear();
    fitBoundsMock.mockClear();
    onMock.mockClear();
    offMock.mockClear();
    eventHandlers = {};
    onMock.mockImplementation((event: string, layer: string, handler: (event: any) => void) => {
      eventHandlers[`${event}:${layer}`] = handler;
    });
    mockMap.getSource.mockClear();
    mockMap.once.mockClear();
    mockMap.isStyleLoaded.mockReturnValue(true);
  });

  it('builds one point feature per target and filters invalid coordinates', () => {
    const geojson = buildAisTargetGeoJSON([
      targetOne,
      targetTwo,
      { ...targetOne, target_id: 111, lat: Number.NaN },
      { ...targetOne, target_id: 222, lon: Number.POSITIVE_INFINITY },
    ]);

    expect(geojson.features).toHaveLength(2);
    expect(geojson.features[0].geometry).toEqual({
      type: 'Point',
      coordinates: [106.0, -2.0],
    });
    expect(geojson.features[0].properties).toMatchObject({
      target_id: 123456789,
      sog_kn: 12,
      cog_deg: 90,
      heading_deg: null,
      label: 'CHITOSE',
      marker_kind: 'arrow',
      marker_color: '#34d399',
      ship_name: 'CHITOSE',
      ship_type: 'cargo',
      destination: 'PELINTUNG',
    });
  });

  it('adds AIS source plus moving-arrow and stationary-circle layers', () => {
    render(<Wrapper targets={[targetOne]} />);

    expect(addSourceMock).toHaveBeenCalledWith(
      'ais-targets',
      expect.objectContaining({ type: 'geojson' }),
    );
    expect(addLayerMock).toHaveBeenCalledWith(
      expect.objectContaining({ id: 'ais-targets-circle', type: 'circle', source: 'ais-targets' }),
    );
    expect(addLayerMock).toHaveBeenCalledWith(
      expect.objectContaining({ id: 'ais-targets-arrow', type: 'symbol', source: 'ais-targets' }),
    );
    expect(onMock).toHaveBeenCalledWith('mousemove', 'ais-targets-arrow', expect.any(Function));
    expect(onMock).toHaveBeenCalledWith('click', 'ais-targets-arrow', expect.any(Function));
  });

  it('fits the map to AIS targets on first non-empty render', () => {
    render(<Wrapper targets={[targetOne, targetTwo]} />);

    expect(fitBoundsMock).toHaveBeenCalledWith(
      [[106.0, -2.1], [106.1, -2.0]],
      { padding: 140, maxZoom: 10, duration: 0 },
    );
  });

  it('updates AIS source data on rerender', () => {
    const { rerender } = render(<Wrapper targets={[targetOne]} />);

    rerender(<Wrapper targets={[targetOne, targetTwo]} />);

    expect(setDataMock).toHaveBeenCalledWith(expect.objectContaining({
      type: 'FeatureCollection',
      features: expect.arrayContaining([
        expect.objectContaining({
          geometry: { type: 'Point', coordinates: [106.1, -2.1] },
        }),
      ]),
    }));
  });

  it('shows a summary plaque on hover and a details card on click', () => {
    render(<Wrapper targets={[targetOne]} />);
    const feature = buildAisTargetGeoJSON([targetOne]).features[0];

    act(() => {
      eventHandlers['mousemove:ais-targets-arrow']({
        point: { x: 100, y: 120 },
        features: [feature],
      });
    });

    expect(screen.getByText(/CHITOSE/)).toBeInTheDocument();
    expect(screen.getByText(/PELINTUNG/)).toBeInTheDocument();

    act(() => {
      eventHandlers['click:ais-targets-arrow']({
        point: { x: 100, y: 120 },
        features: [feature],
      });
    });

    expect(screen.getByText('Vessel details')).toBeInTheDocument();
    expect(screen.getByText(/Cargo/)).toBeInTheDocument();
  });
});

describe('fetchLatestAisTargets', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('fetches AIS latest from the AIS Twin proxy by default', async () => {
    const fetchMock = vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: true,
      json: async () => ({
        provider: 'ais-twin',
        generated_at_utc: '2026-06-11T00:00:00Z',
        target_count: 0,
        targets: [],
      }),
    } as Response);

    await fetchLatestAisTargets();

    expect(fetchMock).toHaveBeenCalledWith('/ais-twin/api/ais/latest');
  });

  it('trims explicit debug base URL before fetching AIS latest', async () => {
    const fetchMock = vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: true,
      json: async () => ({
        provider: 'ais-twin',
        generated_at_utc: '2026-06-11T00:00:00Z',
        target_count: 0,
        targets: [],
      }),
    } as Response);

    await fetchLatestAisTargets('http://127.0.0.1:8095/');

    expect(fetchMock).toHaveBeenCalledWith('http://127.0.0.1:8095/api/ais/latest');
  });

  it('passes chart region when fetching AIS latest', async () => {
    const fetchMock = vi.spyOn(globalThis, 'fetch').mockResolvedValue({
      ok: true,
      json: async () => ({
        provider: 'ais-twin',
        generated_at_utc: '2026-06-11T00:00:00Z',
        target_count: 0,
        targets: [],
      }),
    } as Response);

    await fetchLatestAisTargets('/ais-twin', 'coastal_archipelago');

    expect(fetchMock).toHaveBeenCalledWith('/ais-twin/api/ais/latest?region=coastal_archipelago');
  });
});
