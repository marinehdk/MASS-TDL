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
        container: 'mass-l3-plugin-route',
        status: 'running',
        health: 'degraded',
        image: 'mass-l2-planner:main',
        expected_image: 'mass-l2-planner:main',
        revision: 'unknown',
        revision_label: 'org.opencontainers.image.revision',
        required_topics: {
          '/route_planning/route_plan': 'ship_interfaces/msg/RoutePlan',
        },
        topic_status: 'unchecked',
        health_required: true,
        ros_domain_id: 42,
      }],
    };
    const summary: RuntimeSummary = {
      mode: 'integration',
      target: 'local',
      active_profile: 'integration-local',
      verdict: 'NO-GO',
      core_services: [],
      plugin_roles: [role],
      gates: [
        {
          name: 'core_services_running',
          passed: false,
          services: {
            'sil-orchestrator': 'running',
            'sil-nodes': 'stopped',
          },
        },
        {
          name: 'single_active_plugin_per_role',
          passed: true,
          roles: [{
            role: 'route_l2',
            active_plugin: 'l2-planner-main',
            running_plugins: ['l2-planner-main'],
            passed: true,
          }],
        },
      ],
    };

    expect(summary.plugin_roles[0].single_instance).toBe(true);
    expect(summary.gates[0].passed).toBe(false);
  });
});
