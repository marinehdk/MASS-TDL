import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import type { RuntimePluginRole } from '../../../api/silApi';
import { PluginRolePanel } from '../PluginRolePanel';

function routeRole(): RuntimePluginRole {
  return {
    role: 'route_l2',
    active_plugin: 'l2-planner-main',
    single_instance: true,
    plugins: [
      {
        id: 'l2-planner-main',
        label: 'L2 Planner Main',
        service: 'plugin-route-l2-main',
        container: 'route',
        status: 'running',
        health: 'degraded',
        image: 'mass-l2-planner:main',
        expected_image: 'mass-l2-planner:main',
        revision: 'unknown',
        revision_label: 'org.opencontainers.image.revision',
        required_topics: { '/route_planning/route_plan': 'ship_interfaces/msg/RoutePlan' },
        topic_status: 'unchecked',
        health_required: true,
        ros_domain_id: 10,
      },
      {
        id: 'tdl-mock-route',
        label: 'TDL Mock Route',
        service: 'plugin-route-tdl-mock',
        container: '',
        status: 'stopped',
        health: 'unknown',
        image: 'mass-l3-plugin-route-mock:local',
        expected_image: 'mass-l3-plugin-route-mock:local',
        revision: 'local',
        revision_label: 'org.opencontainers.image.revision',
        required_topics: { '/l2/planned_route': 'nav_msgs/msg/Path' },
        topic_status: 'unchecked',
        health_required: false,
        ros_domain_id: 42,
      },
    ],
  };
}

describe('PluginRolePanel', () => {
  it('switches selected plugin through role-level callback', () => {
    const onSwitch = vi.fn();
    render(
      <PluginRolePanel
        role={routeRole()}
        onSwitch={onSwitch}
      />,
    );

    fireEvent.change(screen.getByLabelText(/L2 航线规划/i), { target: { value: 'tdl-mock-route' } });
    fireEvent.click(screen.getByRole('button', { name: /Switch route_l2/i }));

    expect(onSwitch).toHaveBeenCalledWith('route_l2', 'tdl-mock-route');
  });

  it('does not render unsupported plugin restart or stop controls', () => {
    render(<PluginRolePanel role={routeRole()} onSwitch={vi.fn()} />);

    expect(screen.queryByRole('button', { name: /Restart l2-planner-main/i })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: /Stop l2-planner-main/i })).not.toBeInTheDocument();
  });

  it('preserves selected plugin across harmless role refetches', () => {
    const { rerender } = render(<PluginRolePanel role={routeRole()} onSwitch={vi.fn()} />);
    const select = screen.getByLabelText(/L2 航线规划/i) as HTMLSelectElement;

    fireEvent.change(select, { target: { value: 'tdl-mock-route' } });
    expect(select.value).toBe('tdl-mock-route');

    const refetchedRole = routeRole();
    refetchedRole.plugins = refetchedRole.plugins.map((plugin) => ({ ...plugin }));
    rerender(<PluginRolePanel role={refetchedRole} onSwitch={vi.fn()} />);

    expect((screen.getByLabelText(/L2 航线规划/i) as HTMLSelectElement).value).toBe('tdl-mock-route');
  });
});
