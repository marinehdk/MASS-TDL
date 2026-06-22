import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { CheckCategoryNav } from '../CheckCategoryNav';

describe('CheckCategoryNav', () => {
  it('shows runtime categories and selects a category', () => {
    const onSelect = vi.fn();

    render(
      <CheckCategoryNav
        selected="mode"
        onSelect={onSelect}
        status={{
          mode: 'ACTIVE',
          core: '4/4',
          plugins: '2/3',
          ros: 'OK',
          safety: 'OK',
          verdict: 'WAIT',
        }}
      />,
    );

    expect(screen.getByRole('button', { name: /运行模式确认/i })).toHaveAttribute('aria-current', 'true');
    expect(screen.getByText('内部核心容器')).toBeInTheDocument();
    expect(screen.getByText('4/4 核心容器')).toBeInTheDocument();
    expect(screen.getByText('4个核心容器检查是否通过')).toBeInTheDocument();
    expect(screen.getByText('外部核心容器')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: /外部核心容器/i }));
    expect(onSelect).toHaveBeenCalledWith('plugins');
  });

  it('marks internal-mode ROS2 as passed when internal topic evidence is green', () => {
    render(
      <CheckCategoryNav
        selected="ros"
        onSelect={vi.fn()}
        status={{
          mode: 'ACTIVE',
          core: '4/4',
          plugins: '0/3',
          ros: 'OK',
          safety: 'OK',
          verdict: 'WAIT',
        }}
        displayMode="internal"
        streaming
        gates={[
          { gate_id: 1, label: '系统物理就绪', passed: true, duration_ms: 1, checks: [], rationale: '' },
          { gate_id: 2, label: '模块脉搏健康', passed: true, duration_ms: 1, checks: [], rationale: '' },
        ]}
        runtimeSummary={{
          mode: 'integration',
          target: 'local',
          active_profile: 'integration-local',
          verdict: 'IDLE',
          core_services: [],
          plugin_roles: [
            {
              role: 'fusion',
              active_plugin: 'yougc-fusion',
              single_instance: true,
              plugins: [
                {
                  id: 'yougc-fusion',
                  label: 'YouGC Fusion',
                  service: 'plugin-fusion-yougc',
                  container: 'mass-l3-plugin-fusion-1',
                  status: 'running',
                  health: 'healthy',
                  image: 'yougc-fusion:20260612',
                  expected_image: 'yougc-fusion:20260612',
                  revision: 'local',
                  revision_label: 'org.opencontainers.image.revision',
                  required_topics: { '/fusion/tracked_targets': 'nmea_interfaces/msg/TrackedTargetArray' },
                  topic_status: 'unchecked',
                  health_required: true,
                  ros_domain_id: 11,
                },
              ],
            },
          ],
          gates: [],
        }}
      />,
    );

    const rosButton = screen.getByRole('button', { name: /ROS2数据链路/i });
    expect(rosButton).toHaveTextContent('通过');
    expect(rosButton).not.toHaveTextContent('检查中');
    expect(rosButton).toHaveTextContent('/sil/own_ship_state');
    expect(rosButton).not.toHaveTextContent('/fusion/tracked_targets');
  });

  it('marks external plugin checks as not applicable in internal mode', () => {
    render(
      <CheckCategoryNav
        selected="plugins"
        onSelect={vi.fn()}
        status={{
          mode: 'ACTIVE',
          core: '4/4',
          plugins: '0/0',
          ros: 'OK',
          safety: 'OK',
          verdict: 'GO',
        }}
        displayMode="internal"
        runtimeSummary={{
          mode: 'internal',
          target: 'local',
          active_profile: 'integration-local',
          verdict: 'GO',
          core_services: [],
          plugin_roles: [
            {
              role: 'route_l2',
              active_plugin: 'tdl-mock-route',
              single_instance: true,
              plugins: [],
            },
            {
              role: 'hydrodynamics',
              active_plugin: 'hydro-fossen',
              single_instance: true,
              plugins: [],
            },
            {
              role: 'fusion',
              active_plugin: 'yougc-fusion',
              single_instance: true,
              plugins: [],
            },
          ],
          gates: [
            {
              name: 'single_active_plugin_per_role',
              passed: false,
              roles: [],
            },
          ],
        }}
      />,
    );

    const pluginsButton = screen.getByRole('button', { name: /外部核心容器/i });
    expect(pluginsButton).toHaveTextContent('通过');
    expect(pluginsButton).toHaveTextContent('0/0 角色容器');
    expect(pluginsButton).toHaveTextContent('内测模式不启用外部角色容器');
    expect(pluginsButton).not.toHaveTextContent('3/3 角色容器');
  });
});
