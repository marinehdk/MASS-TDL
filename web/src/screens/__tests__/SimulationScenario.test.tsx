import { beforeEach, describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/react';

const mocks = vi.hoisted(() => ({
  listScenarios: [] as any[],
  details: {} as Record<string, any>,
  setScenario: vi.fn(),
  setYamlValidation: vi.fn(),
  silMapView: vi.fn(),
  plannedRouteLayer: vi.fn(),
}));

vi.mock('../../map/SilMapView', () => ({
  SilMapView: (props: any) => {
    mocks.silMapView(props);
    return <div data-testid="mock-sil-map-view" />;
  },
}));

vi.mock('../../map/PlannedRouteLayer', () => ({
  PlannedRouteLayer: (props: any) => {
    mocks.plannedRouteLayer(props);
    return (
      <div
        data-testid="planned-route-layer"
        data-waypoint-count={props.waypoints.length}
      />
    );
  },
}));

vi.mock('../../hooks/useMapInteraction', () => ({
  useMapInteraction: vi.fn(),
}));

vi.mock('../../hooks/useSchemaValidation', () => ({
  useSchemaValidation: () => ({ valid: true, errors: [] }),
}));

vi.mock('../../api/silApi', () => ({
  useListScenariosQuery: () => ({ data: mocks.listScenarios }),
  useGetScenarioQuery: (id: string, options?: { skip?: boolean }) => (
    options?.skip ? { data: undefined } : { data: mocks.details[id] }
  ),
  useCreateScenarioMutation: () => [vi.fn()],
  useUpdateScenarioMutation: () => [vi.fn()],
}));

vi.mock('../../store', () => {
  const useScenarioStore = (selector: any) => selector({
    setScenario: mocks.setScenario,
  });
  useScenarioStore.getState = () => ({
    setYamlValidation: mocks.setYamlValidation,
  });
  return { useScenarioStore };
});

import { SimulationScenario } from '../SimulationScenario';

const buildSafeRouteYaml = () => {
  const route = Array.from({ length: 324 }, (_, i) => {
    const latitude = Number((-1.5 - i * 0.001).toFixed(6));
    const longitude = Number((105.12 + i * 0.001).toFixed(6));
    return `    - { latitude: ${latitude}, longitude: ${longitude}, target_sog_kn: 29.16 }`;
  }).join('\n');

  return `
title: safe_route
ownShip:
  initial:
    position: {latitude: -1.5, longitude: 105.12}
    heading: 63.6
    sog: 29.16
  nominalRoute:
${route}
metadata:
  odd_cell:
    domain: coastal_archipelago
`;
};

describe('SimulationScenario ODD-filtered scenario library', () => {
  beforeEach(() => {
    mocks.setScenario.mockClear();
    mocks.setYamlValidation.mockClear();
    mocks.silMapView.mockClear();
    mocks.plannedRouteLayer.mockClear();
    mocks.listScenarios = [
      {
        id: 'safe_route',
        name: 'Safe Route',
        encounter_type: 'transit',
        folder: '集成测试',
        is_baseline: false,
        latitude: -1.5,
        longitude: 105.12,
        odd_domain: 'coastal_archipelago',
      },
    ];
    mocks.details = {
      safe_route: {
        hash: 'safe-route-hash',
        yaml_content: buildSafeRouteYaml(),
      },
    };
  });

  it('shows safe_route.yaml in the coastal archipelago library and loads its L2 route on the map', async () => {
    render(<SimulationScenario />);

    expect(screen.queryByTestId('safe-route-scenario-card')).not.toBeInTheDocument();

    const domainSelect = screen.getByDisplayValue('港口水域');
    fireEvent.change(domainSelect, { target: { value: 'coastal_archipelago' } });

    fireEvent.click(screen.getByTestId('scenario-tab-library'));

    expect(await screen.findByText('集成测试')).toBeInTheDocument();
    expect(screen.queryByText('safe_route.yaml')).not.toBeInTheDocument();

    fireEvent.click(screen.getByText('集成测试'));

    const scenario = await screen.findByTestId('scenario-card-safe_route');
    expect(scenario).toHaveTextContent('safe_route.yaml');
    fireEvent.click(scenario);

    await waitFor(() => {
      expect(mocks.setScenario).toHaveBeenCalledWith('safe_route', 'safe-route-hash');
    });
    await waitFor(() => {
      expect(screen.getByTestId('planned-route-layer')).toHaveAttribute('data-waypoint-count', '324');
    });
    await waitFor(() => {
      const calls = mocks.silMapView.mock.calls;
      const lastMapProps = calls[calls.length - 1]?.[0];
      expect(lastMapProps.previewData.ownShip).toMatchObject({
        lat: -1.5,
        lon: 105.12,
      });
    });
  });
});
