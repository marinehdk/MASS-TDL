import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { DecisionChainTimingBar } from '../DecisionChainTimingBar';
import type { ModulePulse } from '../../../types';

function makePulse(moduleId: number, latencyMs: number): ModulePulse {
  return { moduleId, state: 1, latencyMs } as any;
}

const nominalPulses: ModulePulse[] = [
  makePulse(1, 0.8),
  makePulse(2, 3.2),
  makePulse(4, 1.1),
  makePulse(6, 2.3),
  makePulse(5, 18.7),
  makePulse(7, 4.1),
];

const slowPulses: ModulePulse[] = [
  makePulse(1, 0.8),
  makePulse(2, 3.2),
  makePulse(4, 1.1),
  makePulse(6, 2.3),
  makePulse(5, 85.0),
  makePulse(7, 4.1),
];

describe('DecisionChainTimingBar', () => {
  it('renders null when no pulses', () => {
    const { container } = render(<DecisionChainTimingBar pulses={[]} />);
    expect(container.firstChild).toBeNull();
  });

  it('renders module latency labels', () => {
    render(<DecisionChainTimingBar pulses={nominalPulses} />);
    expect(screen.getByText(/M1/)).toBeInTheDocument();
    expect(screen.getByText(/M5/)).toBeInTheDocument();
  });

  it('shows total latency', () => {
    render(<DecisionChainTimingBar pulses={nominalPulses} />);
    expect(screen.getByTestId('timing-total').textContent).toMatch(/30/);
  });

  it('M5 segment is red when > 50ms', () => {
    render(<DecisionChainTimingBar pulses={slowPulses} />);
    const m5seg = screen.getByTestId('timing-seg-5');
    expect(m5seg.getAttribute('data-color')).toBe('red');
  });

  it('whole bar data-overload when total > 100ms', () => {
    const overloadPulses = [makePulse(5, 120)];
    render(<DecisionChainTimingBar pulses={overloadPulses} />);
    expect(screen.getByTestId('timing-bar').getAttribute('data-overload')).toBe('true');
  });
});
