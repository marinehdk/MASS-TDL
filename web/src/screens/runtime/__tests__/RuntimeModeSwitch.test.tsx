import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { RuntimeModeSwitch } from '../RuntimeModeSwitch';

describe('RuntimeModeSwitch', () => {
  it('shows internal and integration as prominent actions', () => {
    const onChange = vi.fn();

    render(<RuntimeModeSwitch mode="internal" onChange={onChange} />);

    expect(screen.getByRole('button', { name: '内测' })).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByRole('button', { name: '集成' })).toHaveAttribute('aria-pressed', 'false');
    fireEvent.click(screen.getByRole('button', { name: '集成' }));
    expect(onChange).toHaveBeenCalledWith('integration');
  });
});
