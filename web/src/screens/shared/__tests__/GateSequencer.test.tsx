import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';
import { GateSequencer } from '../GateSequencer';
import type { GateSSEEvent } from '../../../types/gateStream';

const mockGates: GateSSEEvent[] = [
  { gate_id: 1, label: 'System Readiness', passed: true, checks: [], duration_ms: 230, rationale: 'ok' },
  { gate_id: 2, label: 'Module Health', passed: false, checks: [], duration_ms: 150, rationale: 'M7 fail' },
];

describe('GateSequencer', () => {
  it('renders 6 gate rows', () => {
    render(<GateSequencer gates={mockGates} streaming={false} focusedGateId={null}
      onGateSelect={() => {}} verdict={null} />);
    for (let i = 1; i <= 6; i++) {
      expect(screen.getByText(new RegExp(`GATE 0${i}`))).toBeDefined();
    }
  });

  it('shows GO verdict banner', () => {
    render(<GateSequencer gates={[]} streaming={false} focusedGateId={null}
      onGateSelect={() => {}} verdict="GO" />);
    expect(screen.getByText('GO')).toBeDefined();
  });

  it('calls onGateSelect on click', () => {
    const fn = vi.fn();
    render(<GateSequencer gates={mockGates} streaming={false} focusedGateId={null}
      onGateSelect={fn} verdict={null} />);
    fireEvent.click(screen.getByText(/GATE 01/));
    expect(fn).toHaveBeenCalledWith(1);
  });
});
