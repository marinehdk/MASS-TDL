import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { PluginRolePanel } from '../PluginRolePanel';

describe('PluginRolePanel', () => {
  it('switches selected plugin through role-level callback', () => {
    const onSwitch = vi.fn();
    render(
      <PluginRolePanel
        role={{
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
        }}
        onSwitch={onSwitch}
        onRestart={vi.fn()}
        onStop={vi.fn()}
      />,
    );

    fireEvent.change(screen.getByLabelText(/L2 航线规划/i), { target: { value: 'tdl-mock-route' } });
    fireEvent.click(screen.getByRole('button', { name: /Switch route_l2/i }));

    expect(onSwitch).toHaveBeenCalledWith('route_l2', 'tdl-mock-route');
  });
});
