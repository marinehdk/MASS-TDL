import { render } from '@testing-library/react';
import { useRef } from 'react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { fetchLatestAisTargets, type AisTarget } from '../../api/aisTwinApi';
import { AisTargetLayer, buildAisTargetGeoJSON } from '../AisTargetLayer';

const addSourceMock = vi.fn();
const addLayerMock = vi.fn();
const setDataMock = vi.fn();

const mockMap = {
  isStyleLoaded: vi.fn(() => true),
  once: vi.fn(),
  addSource: addSourceMock,
  addLayer: addLayerMock,
  getSource: vi.fn(() => ({ setData: setDataMock })),
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
};

const targetTwo: AisTarget = {
  target_id: 987654321,
  lat: -2.1,
  lon: 106.1,
  sog_kn: 8,
  cog_deg: 135,
  heading_deg: 140,
  source_sensor: 'ais',
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
      label: 'AIS 123456789',
    });
  });

  it('adds AIS source and circle/label layers', () => {
    render(<Wrapper targets={[targetOne]} />);

    expect(addSourceMock).toHaveBeenCalledWith(
      'ais-targets',
      expect.objectContaining({ type: 'geojson' }),
    );
    expect(addLayerMock).toHaveBeenCalledWith(
      expect.objectContaining({ id: 'ais-targets-circle', type: 'circle', source: 'ais-targets' }),
    );
    expect(addLayerMock).toHaveBeenCalledWith(
      expect.objectContaining({ id: 'ais-targets-label', type: 'symbol', source: 'ais-targets' }),
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
});
