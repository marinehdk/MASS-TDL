import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { ColregsDecisionTree } from '../ColregsDecisionTree';

describe('ColregsDecisionTree', () => {
  it('verifies active and pending layers at t=30', () => {
    render(<ColregsDecisionTree currentTimeSec={30} />);

    // L1 (level 1) should be active
    const layer1 = screen.getByTestId('colregs-layer-1');
    expect(layer1).toBeInTheDocument();
    expect(layer1.querySelector('.decision-card-status-badge')?.textContent).toBe('Active');
    expect(layer1.querySelector('.decision-card')?.className).toContain('active');

    // L2 (level 2) should be pending
    const layer2 = screen.getByTestId('colregs-layer-2');
    expect(layer2).toBeInTheDocument();
    expect(layer2.querySelector('.decision-card-status-badge')?.textContent).toBe('Pending');
    expect(layer2.querySelector('.decision-card')?.className).toContain('pending');

    // L5 (level 5) should be pending
    const layer5 = screen.getByTestId('colregs-layer-5');
    expect(layer5.querySelector('.decision-card-status-badge')?.textContent).toBe('Pending');
    expect(layer5.querySelector('.decision-card')?.className).toContain('pending');
  });

  it('verifies active and pending layers at t=160', () => {
    render(<ColregsDecisionTree currentTimeSec={160} />);

    // L1 should be active
    const layer1 = screen.getByTestId('colregs-layer-1');
    expect(layer1.querySelector('.decision-card-status-badge')?.textContent).toBe('Active');

    // L5 should be active
    const layer5 = screen.getByTestId('colregs-layer-5');
    expect(layer5.querySelector('.decision-card-status-badge')?.textContent).toBe('Active');
    expect(layer5.querySelector('.decision-card')?.className).toContain('active');
  });

  it('verifies specific transition edge cases', () => {
    // Test t=0: all should be pending
    const { rerender } = render(<ColregsDecisionTree currentTimeSec={0} />);
    for (let i = 1; i <= 5; i++) {
      const layer = screen.getByTestId(`colregs-layer-${i}`);
      expect(layer.querySelector('.decision-card-status-badge')?.textContent).toBe('Pending');
    }

    // Test t=25: L1 active, others pending
    rerender(<ColregsDecisionTree currentTimeSec={25} />);
    expect(screen.getByTestId('colregs-layer-1').querySelector('.decision-card-status-badge')?.textContent).toBe('Active');
    expect(screen.getByTestId('colregs-layer-2').querySelector('.decision-card-status-badge')?.textContent).toBe('Pending');

    // Test t=49: L1, L2, L3 active, others pending
    rerender(<ColregsDecisionTree currentTimeSec={49} />);
    expect(screen.getByTestId('colregs-layer-1').querySelector('.decision-card-status-badge')?.textContent).toBe('Active');
    expect(screen.getByTestId('colregs-layer-2').querySelector('.decision-card-status-badge')?.textContent).toBe('Active');
    expect(screen.getByTestId('colregs-layer-3').querySelector('.decision-card-status-badge')?.textContent).toBe('Active');
    expect(screen.getByTestId('colregs-layer-4').querySelector('.decision-card-status-badge')?.textContent).toBe('Pending');

    // Test t=52: L1, L2, L3, L4 active, L5 pending
    rerender(<ColregsDecisionTree currentTimeSec={52} />);
    expect(screen.getByTestId('colregs-layer-4').querySelector('.decision-card-status-badge')?.textContent).toBe('Active');
    expect(screen.getByTestId('colregs-layer-5').querySelector('.decision-card-status-badge')?.textContent).toBe('Pending');

    // Test t=152: L5 active
    rerender(<ColregsDecisionTree currentTimeSec={152} />);
    expect(screen.getByTestId('colregs-layer-5').querySelector('.decision-card-status-badge')?.textContent).toBe('Active');
  });
});
