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
        }]}
        onRestart={onRestart}
        onStopCoreStack={vi.fn()}
      />,
    );

    fireEvent.click(screen.getByRole('button', { name: /Restart sil-orchestrator/i }));

    expect(onRestart).toHaveBeenCalledWith('sil-orchestrator');
    expect(screen.queryByRole('button', { name: /Stop sil-orchestrator/i })).not.toBeInTheDocument();
    expect(screen.getByRole('button', { name: /Stop Core Stack/i })).toBeInTheDocument();
  });
});
