import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { DiagnosticCanvas } from '../DiagnosticCanvas';

describe('DiagnosticCanvas', () => {
  it('shows GO overlay when verdict is GO', () => {
    render(<DiagnosticCanvas focusedGateId={null} gates={[]} scenarioYaml="" storedYaml="" verdict="GO" countdown={3} />);
    expect(screen.getByText(/ALL GATES CLEAR/)).toBeDefined();
    expect(screen.getByText(/Auto-activating in 3/)).toBeDefined();
  });

  it('shows idle spinner when no focusedGateId', () => {
    render(<DiagnosticCanvas focusedGateId={null} gates={[]} scenarioYaml="" storedYaml="" verdict={null} countdown={0} />);
    expect(screen.getByText('Initializing...')).toBeDefined();
  });
});
