import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { CoreServicePanel } from '../CoreServicePanel';

describe('CoreServicePanel', () => {
  it('shows restart per core service and no single-service stop', () => {
    const onRestart = vi.fn();
    render(
      <CoreServicePanel
        services={[{
          id: 'sil-orchestrator',
          service: 'sil-orchestrator',
          class: 'core_service',
          container_name: 'mass-l3-sil-sil-orchestrator-1',
          status: 'running',
          health: 'healthy',
          image: 'mass-l3-sil-sil-orchestrator',
          allowed_actions: ['restart'],
        }, {
          id: 'sil-nodes',
          service: 'sil-nodes',
          class: 'core_service',
          container_name: 'mass-l3-sil-sil-nodes-1',
          status: 'stopped',
          health: 'unknown',
          image: 'mass-l3-sil-sil-nodes',
          allowed_actions: ['restart'],
        }]}
        onRestart={onRestart}
      />,
    );

    expect(screen.getByTestId('runtime-core-grid')).toHaveStyle({
      gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
    });
    const runningCard = screen.getByTestId('core-service-card-sil-orchestrator');
    const stoppedCard = screen.getByTestId('core-service-card-sil-nodes');
    expect(runningCard).toBeInTheDocument();
    expect(runningCard).toHaveTextContent('运行');
    expect(runningCard).not.toHaveTextContent('running / unknown');
    expect(stoppedCard).toHaveTextContent('停止');

    fireEvent.click(screen.getAllByRole('button', { name: '重启' })[0]);

    expect(onRestart).toHaveBeenCalledWith('sil-orchestrator');
    expect(screen.queryByRole('button', { name: /Stop sil-orchestrator/i })).not.toBeInTheDocument();
    expect(screen.queryByRole('button', { name: /Stop Core Stack/i })).not.toBeInTheDocument();
  });
});
