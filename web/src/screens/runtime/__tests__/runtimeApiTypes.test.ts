import { describe, expect, it } from 'vitest';
import type { RuntimeSummary, RuntimePluginRole } from '../../../api/silApi';

describe('runtime API types', () => {
  it('represents runtime summary and plugin roles', () => {
    const role: RuntimePluginRole = {
      role: 'route_l2',
      active_plugin: 'l2-planner-main',
      single_instance: true,
      plugins: [{
        id: 'l2-planner-main',
        label: 'L2 Planner Main',
        service: 'plugin-route-l2-main',
        container_name: 'mass-l3-plugin-route',
        status: 'running',
        health: 'degraded',
        image: 'mass-l2-planner:main',
        revision: 'unknown',
        required_topics: [{ name: '/route_planning/route_plan', type: 'ship_interfaces/msg/RoutePlan', status: 'missing' }],
      }],
    };
    const summary: RuntimeSummary = {
      mode: 'integration',
      target: 'local',
      active_profile: 'integration-local',
      verdict: 'NO-GO',
      core_services: [],
      plugin_roles: [role],
      gates: [],
    };

    expect(summary.plugin_roles[0].single_instance).toBe(true);
  });
});
