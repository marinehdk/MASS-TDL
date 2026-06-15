import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { EvidenceStrip } from '../EvidenceStrip';

describe('EvidenceStrip', () => {
  it('shows fallback when runtime evidence is absent', () => {
    render(<EvidenceStrip />);

    expect(screen.getByText('No runtime evidence')).toBeInTheDocument();
  });
});
