import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import { RuntimeActionLog } from '../RuntimeActionLog';

describe('RuntimeActionLog', () => {
  it('renders runtime action entries', () => {
    render(<RuntimeActionLog entries={[{ time: '12:00:00', message: 'switch route_l2', level: 'info' }]} />);

    expect(screen.getByRole('heading', { name: 'Runtime Actions' })).toBeInTheDocument();
    expect(screen.getByText('switch route_l2')).toBeInTheDocument();
  });
});
