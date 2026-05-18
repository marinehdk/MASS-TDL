import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { QuickFixPanel } from '../QuickFixPanel';

describe('QuickFixPanel', () => {
  it('returns null when no gates failed', () => {
    const gates = [{ gate_id: 1, label: 'Test', passed: true, checks: [], duration_ms: 10, rationale: '' }];
    const { container } = render(
      <QuickFixPanel focusedGateId={1} gates={gates} scenarioId="test" runId="r1" onFixApplied={() => {}} />
    );
    expect(container.innerHTML).toBe('');
  });

  it('renders quick fix buttons when gate failed', () => {
    const gates = [{ gate_id: 1, label: 'Test', passed: false, checks: [], duration_ms: 10, rationale: '' }];
    render(
      <QuickFixPanel focusedGateId={1} gates={gates} scenarioId="test" runId="r1" onFixApplied={() => {}} />
    );
    expect(screen.getByText('QUICK FIX')).toBeDefined();
    expect(screen.getByText(/Restart All SIL Services/)).toBeDefined();
  });
});
